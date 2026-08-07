#!/usr/bin/env python3
"""
Read-side queries over the database that mqtt_sql_consumer.py fills.

Usage:
    python query_database.py                      # last 24 h
    python query_database.py --hours 6            # different window
    python query_database.py --db test_v2.db      # different database
    python query_database.py --export out.json    # also export to JSON

Two facts shape every query in here:

1. The consumer stores ONE ROW PER MAGNITUDE (moisture_pct, moisture_raw, lux),
   all three sharing the same `timestamp`. Seeing a complete reading means
   pivoting with CASE WHEN, grouped by timestamp.
2. SQLite stores CURRENT_TIMESTAMP in UTC. That is why filters compare against
   datetime('now', ...), which is UTC too, and 'localtime' shows up only when
   displaying. Comparing against a Python datetime.now() would return no rows.
"""

import argparse
import json
import sqlite3

from tabulate import tabulate

DEFAULT_DB_PATH = "iot_data.db"

# Firmware publish interval (src/main.cpp). A larger distance between two
# consecutive readings means the ingest was down.
PUBLISH_INTERVAL_S = 30
GAP_THRESHOLD_S = PUBLISH_INTERVAL_S * 3


def fmt(value, digits=1):
    """
    Format a number for display.

    Explicit None check, never truthiness: 0.0 is falsy, and lux is legitimately
    0.0 every night. A truthiness check would hide the reading as missing.
    """
    return "-" if value is None else f"{value:.{digits}f}"


class QueryTool:
    def __init__(self, db_path=DEFAULT_DB_PATH):
        self.conn = sqlite3.connect(db_path)
        self.conn.row_factory = sqlite3.Row

    def close(self):
        self.conn.close()

    def __enter__(self):
        return self

    def __exit__(self, *exc):
        self.close()

    # ── Queries ──────────────────────────────────────────────────────────────

    def overview(self):
        """How much data there is, since when, and the range of each magnitude."""
        rows = self.conn.execute('''
            SELECT sensor_type,
                   unit,
                   COUNT(*)   AS n,
                   AVG(value) AS avg_value,
                   MIN(value) AS min_value,
                   MAX(value) AS max_value
            FROM sensor_data
            WHERE value IS NOT NULL
            GROUP BY sensor_type, unit
            ORDER BY sensor_type
        ''').fetchall()

        span = self.conn.execute('''
            SELECT COUNT(DISTINCT timestamp)              AS readings,
                   datetime(MIN(timestamp), 'localtime')  AS first_seen,
                   datetime(MAX(timestamp), 'localtime')  AS last_seen
            FROM sensor_data
        ''').fetchone()

        print("\nOVERVIEW")
        if not span['readings']:
            print("  (empty database)")
            return

        print(f"  Readings: {span['readings']}  |  "
              f"From: {span['first_seen']}  ->  To: {span['last_seen']}  (local time)")
        print(tabulate(
            [[r['sensor_type'], r['unit'], r['n'],
              fmt(r['avg_value'], 2), fmt(r['min_value']), fmt(r['max_value'])]
             for r in rows],
            headers=['Magnitude', 'Unit', 'Rows', 'Average', 'Min', 'Max']))

    def latest(self, limit=10):
        """Last N complete readings, one row each."""
        rows = self.conn.execute('''
            SELECT datetime(timestamp, 'localtime') AS local_time,
                   MAX(CASE WHEN sensor_type = 'moisture_pct'  THEN value END) AS percent,
                   MAX(CASE WHEN sensor_type = 'moisture_raw'  THEN value END) AS raw,
                   MAX(CASE WHEN sensor_type = 'lux'           THEN value END) AS lux,
                   MAX(raw_payload)                                            AS payload
            FROM sensor_data
            GROUP BY timestamp
            ORDER BY timestamp DESC
            LIMIT ?
        ''', (limit,)).fetchall()

        data = []
        for r in rows:
            # `state` only lives inside the original JSON: the schema gave it no column.
            try:
                state = json.loads(r['payload']).get('state', '-')
            except (TypeError, json.JSONDecodeError):
                state = '-'
            data.append([r['local_time'], fmt(r['percent'], 0), fmt(r['raw'], 0),
                         fmt(r['lux']), state])

        print(f"\nLAST {len(data)} READINGS")
        print(tabulate(data,
                       headers=['Local time', 'Moisture %', 'Raw ADC', 'Lux', 'State']))

    def hourly(self, hours=24):
        """Hourly average, the same shape Grafana will display later."""
        rows = self.conn.execute('''
            SELECT strftime('%Y-%m-%d %H:00', timestamp, 'localtime') AS hour_local,
                   COUNT(DISTINCT timestamp)                          AS readings,
                   AVG(CASE WHEN sensor_type = 'moisture_pct' THEN value END) AS moisture,
                   AVG(CASE WHEN sensor_type = 'lux'          THEN value END) AS lux
            FROM sensor_data
            WHERE timestamp >= datetime('now', ?)
            GROUP BY hour_local
            ORDER BY hour_local DESC
        ''', (f'-{hours} hours',)).fetchall()

        # 3600/30 = 120 readings is a full hour with no data loss.
        expected = 3600 // PUBLISH_INTERVAL_S

        print(f"\nHOURLY AVERAGE - last {hours} h")
        print(tabulate(
            [[r['hour_local'], fmt(r['moisture']), fmt(r['lux']),
              f"{r['readings']}/{expected}"]
             for r in rows],
            headers=['Local hour', 'Moisture %', 'Lux', 'Readings']))

    def gaps(self, hours=24, threshold_s=GAP_THRESHOLD_S):
        """
        Holes in the series: stretches where the consumer was not listening.

        Not cosmetic. It is the only way to know an average was computed over
        incomplete data.
        """
        rows = self.conn.execute('''
            WITH readings AS (
                SELECT DISTINCT timestamp
                FROM sensor_data
                WHERE timestamp >= datetime('now', ?)
            ),
            deltas AS (
                SELECT timestamp,
                       LAG(timestamp) OVER (ORDER BY timestamp) AS previous
                FROM readings
            )
            SELECT datetime(previous,  'localtime') AS gap_start,
                   datetime(timestamp, 'localtime') AS gap_end,
                   CAST((julianday(timestamp) - julianday(previous)) * 86400 AS INTEGER) AS seconds
            FROM deltas
            WHERE previous IS NOT NULL
              AND (julianday(timestamp) - julianday(previous)) * 86400 > ?
            ORDER BY seconds DESC
        ''', (f'-{hours} hours', threshold_s)).fetchall()

        print(f"\nGAPS > {threshold_s}s - last {hours} h")
        if not rows:
            print(f"  None: the series is complete "
                  f"(one reading every ~{PUBLISH_INTERVAL_S}s)")
            return

        print(tabulate(
            [[r['gap_start'], r['gap_end'],
              f"{r['seconds']}s" if r['seconds'] < 120 else f"{r['seconds'] // 60} min"]
             for r in rows],
            headers=['From', 'To', 'Duration']))

    def export_json(self, path, hours=24):
        """Export complete readings to JSON, timestamp as ISO-8601 UTC."""
        rows = self.conn.execute('''
            SELECT replace(timestamp, ' ', 'T') || 'Z' AS ts_utc,
                   MAX(CASE WHEN sensor_type = 'moisture_pct' THEN value END) AS percent,
                   MAX(CASE WHEN sensor_type = 'moisture_raw' THEN value END) AS raw,
                   MAX(CASE WHEN sensor_type = 'lux'          THEN value END) AS lux
            FROM sensor_data
            WHERE timestamp >= datetime('now', ?)
            GROUP BY timestamp
            ORDER BY timestamp
        ''', (f'-{hours} hours',)).fetchall()

        with open(path, 'w') as f:
            json.dump([dict(r) for r in rows], f, indent=2)

        print(f"\n{len(rows)} readings exported to {path}")


def main():
    parser = argparse.ArgumentParser(
        description="Queries over the mint monitor database")
    parser.add_argument("--db", default=DEFAULT_DB_PATH,
                        help=f"database to query (default: {DEFAULT_DB_PATH})")
    parser.add_argument("--hours", type=int, default=24,
                        help="time window in hours (default: 24)")
    parser.add_argument("--limit", type=int, default=10,
                        help="how many readings to list in detail (default: 10)")
    parser.add_argument("--export", metavar="FILE",
                        help="also export the window to a JSON file")
    args = parser.parse_args()

    with QueryTool(args.db) as tool:
        tool.overview()
        tool.latest(args.limit)
        tool.hourly(args.hours)
        tool.gaps(args.hours)
        if args.export:
            tool.export_json(args.export, args.hours)


if __name__ == "__main__":
    main()
