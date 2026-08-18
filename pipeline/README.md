# Pipeline de datos

Guarda en SQLite las lecturas que el ESP32 publica en HiveMQ Cloud.

```
ESP32 --MQTT/TLS--> HiveMQ Cloud --> mqtt_sql_consumer.py --> SQLite
```

## How to run

```bash
cd pipeline
python3 -m venv .venv
.venv/bin/pip install -r requirements.txt
cp .env.example .env      # completar con las credenciales de HiveMQ
.venv/bin/python mqtt_sql_consumer.py
```

No hace falta el hardware, alcanza con las credenciales del broker.

```bash
.venv/bin/python query_database.py --hours 6   # ver los datos
.venv/bin/python check_pipeline.py             # chequeo de salud, sale 0 o 1
```

Los tres usan `iot_data.db` como ruta relativa: correrlos desde esta carpeta, o
pasarles `--db` con ruta absoluta. Ni `.env` ni la base van a git.

##  Payload

`menta/moisture`, cada 30 s, con `retain=true`:

```json
{"percent": 42, "raw": 2100, "state": "MOIST", "lux": 350.0}
```

- `percent`: humedad 0-100, promediada sobre unos 10 s
- `raw`: ADC crudo, se mueve al revés que `percent`
- `state`: CRITICAL / DRY / LOW / MOIST / WET
- `lux`: `null` cuando el BH1750 no responde

Cada mensaje se guarda como tres filas en `sensor_data`, una por magnitud
(`moisture_pct`, `moisture_raw`, `lux`), más el JSON entero en `raw_payload`.

## Algunos detalles

- El `timestamp` es la hora de llegada, en UTC. El ESP32 no tiene RTC, así que
  un hueco en la serie no se recupera. `query_database.py` los lista.
- El mensaje retenido se descarta al conectar: es el último valor reenviado, no
  una medición nueva.
- `device_id` es siempre `unknown`, el firmware todavía no lo manda.
- La conexión a SQLite se abre una sola vez. Todo corre dentro del loop de red
  de paho y una escritura lenta frena al cliente MQTT.
