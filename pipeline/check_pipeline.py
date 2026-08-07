#!/usr/bin/env python3
"""
Pipeline health check: is the database well formed and is data still arriving?

Replaces test_system.py, which tested the MQTT connection and published fake
data against a public broker. What survived are the three checks that actually
matter once the consumer runs unattended:

    1. The schema is the one the queries expect
    2. Data is arriving NOW (not "a row existed at some point")
    3. Values are plausible and queries stay fast

Usage:
    python check_pipeline.py
    python check_pipeline.py --db test_v2.db
    python check_pipeline.py --max-age 300      # tolerate 5 min without data

Exits 0 when everything passes and 1 on any failure, so it works from cron or CI.
"""

import argparse
import os
import sqlite3
import sys
import time

# ANSI colours, only when stdout is a terminal. Redirecting to a log file or
# piping into grep must not inject escape sequences into the text. NO_COLOR is
# the usual opt-out convention.
USE_COLOR = sys.stdout.isatty() and os.getenv("NO_COLOR") is None

RED = "\033[31m"
YELLOW = "\033[33m"
GREEN = "\033[32m"
RESET = "\033[0m"


def color(text, code):
    return f"{code}{text}{RESET}" if USE_COLOR else text

# Firmware publish interval (src/main.cpp).
PUBLISH_INTERVAL_S = 30

# Three missed publishes in a row means something is wrong, not just jitter.
DEFAULT_MAX_AGE_S = PUBLISH_INTERVAL_S * 3

# Anything slower than this on a table of a few million rows means an index is
# missing or a query stopped using one.
SLOW_QUERY_MS = 50

EXPECTED_COLUMNS = {
    'id', 'topic', 'device_id', 'sensor_type', 'value',
    'unit', 'timestamp', 'mqtt_timestamp', 'raw_payload',
}

EXPECTED_SENSOR_TYPES = {'moisture_pct', 'moisture_raw', 'lux'}


class PipelineCheck:
    def __init__(self, db_path, max_age_s):
        self.db_path = db_path
        self.max_age_s = max_age_s
        self.conn = sqlite3.connect(db_path)
        self.conn.row_factory = sqlite3.Row
        self.passed = 0
        self.failed = 0
        self.warnings = 0

    def close(self):
        self.conn.close()

    # ── Reporting ────────────────────────────────────────────────────────────

    def header(self, text):
        print("\n" + "=" * 62)
        print(f"  {text}")
        print("=" * 62)

    def ok(self, text):
        print(f"  {color('[PASS]', GREEN)} {text}")
        self.passed += 1

    def fail(self, text):
        print(f"  {color('[FAIL]', RED)} {text}")
        self.failed += 1

    def warn(self, text):
        print(f"  {color('[WARN]', YELLOW)} {text}")
        self.warnings += 1

    def info(self, text):
        print(f"  [INFO] {text}")

    # ── Checks ───────────────────────────────────────────────────────────────

    def check_schema(self):
        """The schema is the one the queries expect."""
        self.header("1. SCHEMA")

        table = self.conn.execute('''
            SELECT name FROM sqlite_master
            WHERE type = 'table' AND name = 'sensor_data'
        ''').fetchone()

        if not table:
            self.fail("Table sensor_data does not exist - has the consumer ever run?")
            return False
        self.ok("Table sensor_data exists")

        columns = {row['name'] for row in
                   self.conn.execute("PRAGMA table_info(sensor_data)")}
        missing = EXPECTED_COLUMNS - columns
        if missing:
            self.fail(f"Missing columns: {', '.join(sorted(missing))}")
            return False
        self.ok(f"All {len(EXPECTED_COLUMNS)} expected columns present")

        indexes = {row['name'] for row in
                   self.conn.execute("PRAGMA index_list(sensor_data)")}
        expected_indexes = {'idx_topic', 'idx_timestamp', 'idx_device'}
        if expected_indexes - indexes:
            self.warn(f"Missing indexes: {', '.join(sorted(expected_indexes - indexes))}")
        else:
            self.ok("All 3 indexes created")

        return True

    def check_freshness(self):
        """
        Data is arriving right now.

        The old test settled for a row existing, which is just as true with the
        consumer down for three days. What matters is the age of the latest
        reading. julianday('now') is UTC, same as CURRENT_TIMESTAMP.
        """
        self.header("2. DATA ARRIVING")

        row = self.conn.execute('''
            SELECT COUNT(DISTINCT timestamp) AS readings,
                   datetime(MAX(timestamp), 'localtime') AS last_seen,
                   CAST((julianday('now') - julianday(MAX(timestamp))) * 86400
                        AS INTEGER) AS age_s
            FROM sensor_data
        ''').fetchone()

        if not row['readings']:
            self.fail("Database is empty")
            return False

        self.info(f"{row['readings']} readings, last one at {row['last_seen']} (local time)")

        age_s = row['age_s']
        if age_s <= self.max_age_s:
            self.ok(f"Latest reading {age_s}s old (tolerance: {self.max_age_s}s)")
        else:
            self.fail(f"Latest reading {age_s}s old - the consumer looks down")

        # A gap in the last hour is not a failure (the data is already lost),
        # but it is exactly what you want to notice.
        gaps = self.conn.execute('''
            WITH readings AS (
                SELECT DISTINCT timestamp FROM sensor_data
                WHERE timestamp >= datetime('now', '-1 hours')
            ),
            deltas AS (
                SELECT timestamp, LAG(timestamp) OVER (ORDER BY timestamp) AS previous
                FROM readings
            )
            SELECT COUNT(*) AS n
            FROM deltas
            WHERE previous IS NOT NULL
              AND (julianday(timestamp) - julianday(previous)) * 86400 > ?
        ''', (DEFAULT_MAX_AGE_S,)).fetchone()

        if gaps['n']:
            self.warn(f"{gaps['n']} gap(s) in the last hour "
                      f"- see: query_database.py --hours 1")
        else:
            self.ok("No gaps in the last hour")

        return True

    def check_data_sanity(self):
        """Stored values are plausible."""
        self.header("3. VALUE SANITY")

        types = {row['sensor_type'] for row in self.conn.execute(
            "SELECT DISTINCT sensor_type FROM sensor_data")}
        unexpected = types - EXPECTED_SENSOR_TYPES
        if unexpected:
            self.warn(f"Unexpected magnitudes: {', '.join(sorted(unexpected))}")
        if EXPECTED_SENSOR_TYPES <= types:
            self.ok(f"All 3 magnitudes present: {', '.join(sorted(EXPECTED_SENSOR_TYPES))}")
        else:
            self.fail(f"Missing magnitudes: {', '.join(sorted(EXPECTED_SENSOR_TYPES - types))}")

        # This is the CHECK constraint the schema does not have yet — until the
        # move to Postgres, it gets verified after the fact instead.
        bad = self.conn.execute('''
            SELECT COUNT(*) AS n FROM sensor_data
            WHERE sensor_type = 'moisture_pct'
              AND (value IS NULL OR value < 0 OR value > 100)
        ''').fetchone()['n']
        if bad:
            self.fail(f"{bad} moisture rows outside the 0-100 range")
        else:
            self.ok("Moisture always within 0-100")

        nulls = self.conn.execute('''
            SELECT COUNT(*) AS n FROM sensor_data WHERE value IS NULL
        ''').fetchone()['n']
        if nulls:
            self.warn(f"{nulls} rows with a NULL value (unrecognised payload)")
        else:
            self.ok("No NULL values")

        return True

    def check_query_latency(self):
        """
        The real queries stay fast.

        Times the three access patterns query_database.py actually uses, not
        made-up queries: if one of them degrades, this is where it shows.
        """
        self.header("4. QUERY LATENCY")

        queries = [
            ("Latest readings (pivot)", '''
                SELECT timestamp,
                       MAX(CASE WHEN sensor_type = 'moisture_pct' THEN value END)
                FROM sensor_data GROUP BY timestamp ORDER BY timestamp DESC LIMIT 10
            '''),
            ("24 h range", '''
                SELECT COUNT(*) FROM sensor_data
                WHERE timestamp >= datetime('now', '-24 hours')
            '''),
            ("Hourly average", '''
                SELECT strftime('%Y-%m-%d %H:00', timestamp, 'localtime') AS hour_local,
                       AVG(value)
                FROM sensor_data
                WHERE sensor_type = 'moisture_pct'
                  AND timestamp >= datetime('now', '-24 hours')
                GROUP BY hour_local
            '''),
        ]

        for name, sql in queries:
            start = time.perf_counter()
            self.conn.execute(sql).fetchall()
            elapsed_ms = (time.perf_counter() - start) * 1000

            if elapsed_ms < SLOW_QUERY_MS:
                self.ok(f"{name}: {elapsed_ms:.1f} ms")
            else:
                self.warn(f"{name}: {elapsed_ms:.1f} ms (> {SLOW_QUERY_MS} ms)")

        return True

    # ── Runner ───────────────────────────────────────────────────────────────

    def run(self):
        print(f"\nDatabase: {self.db_path}")

        # Nothing below makes sense without a valid schema.
        if not self.check_schema():
            self.summary()
            return False

        self.check_freshness()
        self.check_data_sanity()
        self.check_query_latency()
        return self.summary()

    def summary(self):
        self.header("SUMMARY")
        print(f"\n  Passed:   {self.passed}")
        print(f"  Failed:   {self.failed}")
        print(f"  Warnings: {self.warnings}\n")

        if self.failed:
            print(color("  FAIL: the pipeline has problems, see above", RED))
        elif self.warnings:
            print(color("  WARNING: the pipeline works, with observations", YELLOW))
        else:
            print(color("  OK: pipeline healthy", GREEN))
        print("=" * 62 + "\n")

        return self.failed == 0


def main():
    parser = argparse.ArgumentParser(
        description="Health check for the MQTT -> SQLite pipeline")
    parser.add_argument("--db", default="iot_data.db",
                        help="database to verify (default: iot_data.db)")
    parser.add_argument("--max-age", type=int, default=DEFAULT_MAX_AGE_S,
                        metavar="SECONDS",
                        help=f"maximum tolerated age of the latest reading "
                             f"(default: {DEFAULT_MAX_AGE_S})")
    args = parser.parse_args()

    check = PipelineCheck(args.db, args.max_age)
    try:
        healthy = check.run()
    finally:
        check.close()

    sys.exit(0 if healthy else 1)


if __name__ == "__main__":
    main()
