#!/usr/bin/env python3
"""
Consumer: HiveMQ MQTT -> SQLite
Stores the readings the firmware publishes to menta/moisture every 30 s.

- paho-mqtt v2 callback API (not the deprecated one)
- Credentials from .env (see .env.example)
- TLS/SSL for HiveMQ Cloud
- Automatic reconnection
- Discards the retained message on connect (it is not a new measurement)

Usage:
    pip install -r requirements.txt
    cp .env.example .env                            # then fill in credentials
    python mqtt_sql_consumer.py
    python mqtt_sql_consumer.py --db test_v2.db     # separate database
    python mqtt_sql_consumer.py --topic menta/#     # other topics
"""

import argparse
import paho.mqtt.client as mqtt
import sqlite3
import json
import logging
import os
import ssl
import sys

from dotenv import load_dotenv

# Credentials come from .env, never from the source: this file goes to git, .env
# does not. See .env.example for the template.
load_dotenv()

MQTT_BROKER   = os.getenv("MQTT_BROKER")
MQTT_PORT     = int(os.getenv("MQTT_PORT", "8883"))
MQTT_USERNAME = os.getenv("MQTT_USERNAME")
MQTT_PASSWORD = os.getenv("MQTT_PASSWORD")
USE_TLS       = os.getenv("MQTT_USE_TLS", "true").lower() == "true"

# Must differ from the firmware's client_id: if two clients share one, the broker
# kicks them in a loop.
MQTT_CLIENT_ID = os.getenv("MQTT_CLIENT_ID", "menta-ingest-01")

DEFAULT_TOPICS = ["menta/moisture"]  # firmware topic (src/net/mqtt_client.cpp)
DEFAULT_DB_PATH = "iot_data.db"

# Logging
logging.basicConfig(
    level=logging.INFO,
    format='%(asctime)s - %(levelname)s - %(message)s'
)
logger = logging.getLogger(__name__)

class MQTTSQLConsumer:
    def __init__(self, db_path=DEFAULT_DB_PATH, topics=None):
        self.db_path = db_path
        self.topics = topics or DEFAULT_TOPICS
        self.mqtt_client = None
        self.conn = None
        # Topics whose retained delivery was already discarded this session.
        self.retained_seen = set()
        self.init_database()

    def init_database(self):
        """Open the single connection used by the whole process, create the table."""
        try:
            # check_same_thread=False so this keeps working if we ever switch to
            # loop_start(), which runs the callbacks on another thread.
            conn = sqlite3.connect(self.db_path, check_same_thread=False)
            self.conn = conn
            cursor = conn.cursor()

            cursor.execute('''
                CREATE TABLE IF NOT EXISTS sensor_data (
                    id INTEGER PRIMARY KEY AUTOINCREMENT,
                    topic TEXT NOT NULL,
                    device_id TEXT,
                    sensor_type TEXT,
                    value REAL,
                    unit TEXT,
                    timestamp DATETIME DEFAULT CURRENT_TIMESTAMP,
                    mqtt_timestamp TEXT,
                    raw_payload TEXT
                )
            ''')
            
            # Indexes for fast lookups
            cursor.execute('''
                CREATE INDEX IF NOT EXISTS idx_topic 
                ON sensor_data(topic)
            ''')
            cursor.execute('''
                CREATE INDEX IF NOT EXISTS idx_timestamp 
                ON sensor_data(timestamp)
            ''')
            # Helps filtering by device. Useless until the firmware sends a real
            # device_id in the payload — today every row lands as 'unknown'.
            cursor.execute('''
                CREATE INDEX IF NOT EXISTS idx_device
                ON sensor_data(device_id)
            ''')
            
            conn.commit()
            logger.info(f"Database ready: {self.db_path}")
        except Exception as e:
            logger.error(f"Database init failed: {e}")
            sys.exit(1)
    
    def extract_rows(self, topic, data):
        """
        Turn a payload into (sensor_type, value, unit) rows.

        The firmware sends several magnitudes in a single frame:
            {"percent":42,"raw":2100,"state":"MOIST","lux":350.0}
        Each one is stored as its own row so they can be charted separately.
        """
        if 'percent' in data:
            rows = [
                ('moisture_pct', data.get('percent'), '%'),
                ('moisture_raw', data.get('raw'), 'adc'),
            ]
            # lux is null when the BH1750 is absent: skip the row instead of
            # storing a 0 that would skew the average.
            if data.get('lux') is not None:
                rows.append(('lux', data.get('lux'), 'lx'))
            return rows

        # Payload that does not match the firmware contract. Stored under an
        # explicit marker rather than a name guessed from the topic ('moisture'),
        # which passed for a real reading. The row stays visible as an anomaly
        # and raw_payload keeps the original message.
        logger.warning(f"Unrecognised payload on {topic}: {data}")
        return [('unknown', None, '')]

    def save_data(self, topic, payload):
        """Store one message as one row per magnitude."""
        try:
            try:
                data = json.loads(payload)
            except json.JSONDecodeError:
                data = {"raw_value": payload}

            device_id = data.get('device_id', data.get('device', 'unknown'))
            mqtt_timestamp = data.get('timestamp', data.get('ts', ''))
            rows = self.extract_rows(topic, data)

            cursor = self.conn.cursor()

            cursor.executemany('''
                INSERT INTO sensor_data
                (topic, device_id, sensor_type, value, unit, mqtt_timestamp, raw_payload)
                VALUES (?, ?, ?, ?, ?, ?, ?)
            ''', [(topic, device_id, sensor_type, value, unit, mqtt_timestamp, payload)
                  for sensor_type, value, unit in rows])

            self.conn.commit()
            summary = " | ".join(f"{t}: {v} {u}".strip() for t, v, u in rows)
            logger.info(f"Stored {topic} | device: {device_id} | {summary}")

        except Exception as e:
            logger.error(f"Insert failed: {e}")

    def close(self):
        """Close the database connection."""
        if self.conn:
            self.conn.close()
            self.conn = None

    def on_connect(self, client, userdata, connect_flags, reason_code, properties):
        """Connection callback - v2 API"""
        if reason_code == 0:
            logger.info("Connected to broker")
            # New session: one retained message per topic arrives again and has
            # to be discarded again (see on_message).
            self.retained_seen.clear()
            for topic in self.topics:
                client.subscribe(topic)
                logger.info(f"Subscribed to {topic}")
        else:
            # paho already renders the reason code as readable text; the local
            # dict that used to be here mapped MQTT 3.1.1 codes while this
            # client runs on VERSION2.
            logger.error(f"Connection refused: {reason_code}")
    
    def on_message(self, client, userdata, msg):
        """Message callback"""
        try:
            payload = msg.payload.decode('utf-8')

            # On subscribe the broker replays the last retained message of each
            # topic. That is an old reading re-sent, not a new measurement:
            # storing it would duplicate the row with the wrong timestamp.
            if msg.retain and msg.topic not in self.retained_seen:
                self.retained_seen.add(msg.topic)
                logger.info(f"Discarded retained message: {msg.topic} -> {payload}")
                return

            logger.debug(f"Message: {msg.topic} -> {payload}")
            self.save_data(msg.topic, payload)
        except Exception as e:
            logger.error(f"Failed to process message: {e}")
    
    def on_disconnect(self, client, userdata, disconnect_flags, reason_code, properties):
        """Disconnection callback - v2 API"""
        if reason_code == 0:
            logger.info("Clean disconnect")
        else:
            logger.warning(f"Unexpected disconnect - code: {reason_code}")
            logger.info("Retrying in 5 seconds")
    
    def on_subscribe(self, client, userdata, mid, reason_code_list, properties):
        """Subscription callback - v2 API"""
        for i, reason_code in enumerate(reason_code_list):
            if reason_code.is_failure:
                logger.error(f"Subscription to topic {i} refused")
            else:
                logger.debug(f"Subscribed to topic {i}")
    
    def start(self):
        """Start the consumer on the v2 API"""
        try:
            # Create the client on the v2 API
            self.mqtt_client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2,
                                           client_id=MQTT_CLIENT_ID)

            # Wire up the callbacks
            self.mqtt_client.on_connect = self.on_connect
            self.mqtt_client.on_message = self.on_message
            self.mqtt_client.on_disconnect = self.on_disconnect
            self.mqtt_client.on_subscribe = self.on_subscribe
            
            # Set up authentication
            if MQTT_USERNAME and MQTT_PASSWORD:
                logger.info(f"Authenticating as {MQTT_USERNAME}")
                self.mqtt_client.username_pw_set(MQTT_USERNAME, MQTT_PASSWORD)

            # Set up TLS/SSL when enabled
            if USE_TLS:
                logger.info("TLS/SSL enabled")
                self.mqtt_client.tls_set(
                    ca_certs=None,
                    certfile=None,
                    keyfile=None,
                    cert_reqs=ssl.CERT_REQUIRED,
                    tls_version=ssl.PROTOCOL_TLSv1_2,
                    ciphers=None
                )
                self.mqtt_client.tls_insecure_set(False)
            
            # Connect
            logger.info(f"Connecting to {MQTT_BROKER}:{MQTT_PORT}")
            self.mqtt_client.connect(MQTT_BROKER, MQTT_PORT, keepalive=60)

            # Loop with automatic reconnection
            self.mqtt_client.loop_forever()

        except Exception as e:
            logger.error(f"Fatal error: {e}")
            sys.exit(1)

    def get_stats(self):
        """Session statistics"""
        try:
            cursor = self.conn.cursor()

            cursor.execute("SELECT COUNT(*) FROM sensor_data")
            total = cursor.fetchone()[0]
            
            cursor.execute("""
                SELECT topic, COUNT(*) as count 
                FROM sensor_data 
                GROUP BY topic
            """)
            by_topic = cursor.fetchall()
            
            cursor.execute("""
                SELECT device_id, COUNT(*) as count 
                FROM sensor_data 
                GROUP BY device_id
            """)
            by_device = cursor.fetchall()
            
            print("\n" + "="*60)
            print("SESSION STATISTICS")
            print("="*60)
            print(f"Total rows stored: {total}")
            print("\nBy topic:")
            for topic, count in by_topic:
                print(f"  - {topic}: {count}")
            print("\nBy device:")
            for device, count in by_device:
                print(f"  - {device}: {count}")
            print("="*60 + "\n")

        except Exception as e:
            logger.error(f"Failed to gather statistics: {e}")


def parse_args():
    parser = argparse.ArgumentParser(
        description="MQTT -> SQLite consumer for the mint monitor")
    parser.add_argument(
        "--db", default=DEFAULT_DB_PATH,
        help=f"destination SQLite file (default: {DEFAULT_DB_PATH}). "
             "Use a different one to experiment without touching the history.")
    parser.add_argument(
        "--topic", action="append", dest="topics", metavar="TOPIC",
        help=f"topic to subscribe to, repeatable "
             f"(default: {', '.join(DEFAULT_TOPICS)})")
    return parser.parse_args()


def main():
    # Fail early with a clear message: without credentials there is nothing to do.
    missing = [name for name in ("MQTT_BROKER", "MQTT_USERNAME", "MQTT_PASSWORD")
               if not os.getenv(name)]
    if missing:
        sys.exit(f"Missing variables in .env: {', '.join(missing)}\n"
                 f"  Copy .env.example to .env and fill it in.")

    args = parse_args()
    consumer = MQTTSQLConsumer(db_path=args.db, topics=args.topics)
    logger.info(f"Database: {args.db} | Topics: {', '.join(consumer.topics)}")

    try:
        consumer.start()
    except KeyboardInterrupt:
        logger.info("Stopping consumer")
        consumer.get_stats()
        logger.info("Consumer stopped")
    finally:
        consumer.close()


if __name__ == "__main__":
    main()