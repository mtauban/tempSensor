# tempSensor
RESTful Temperature sensor and relay switch based on ESP8266 with OTA support

## Usage
In Home Assistant `configuration.yaml`, sensor and switch can be added by the
following

### Sensor
```yaml
sensor:
- platform: rest
  resource: http://192.168.x.xxx/state
  name: "ESP Temp Sensor"
  value_template: '{{ value_json.temp }}'
  unit_of_measurement: "°C"
```

### Switch
```yaml
switch:
- platform: rest
  resource: http://192.168.x.xxx/state
  body_on: '{"status": 1}'
  body_off: '{"status": 0}'
  is_on_template: '{{ value_json.status == 1 }}'
  name: 'Boiler'
```

## TODO
- add http config possibility
- clean code
