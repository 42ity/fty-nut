# fty-nut

fty-nut is a family of agents responsible for 42ITy interaction with NUT (see
[http://www.networkupstools.org]) including both collection of device data
and configuration of NUT to monitor new devices as assets are created.

## To build fty-nut project run:

```bash
./autogen.sh
./configure
make
make check # to run self-test
```
Compilation of fty-nut creates two binaries _fty-nut_ and _fty-nut-configurator_, which are run by systemd service.

## How to run

To run fty-nut project:

* from within the source tree, run:

```bash
./src/fty-nut --mapping-file <path_to_mapping_file> --state-file <path_to_state_file>
./src/fty-nut --mapping-file /usr/share/fty-nut/mapping.conf --state-file /var/lib/fty/fty-nut/state_file

./src/fty-nut-configurator
```

* from an installed base, using systemd, run:

```bash
systemctl start fty-nut
systemctl start fty-nut-configurator
```

### Configuration file

To configure fty-nut, a two configuration files exist: _fty-nut.cfg_ and _fty-nut-configurator.cfg_.
Both contain standard configuration directives, under the server sections. Additional parameter

* fty-nut.cfg
  * polling_interval - polling interval in seconds. Default value: 30 s

### Mapping file
Mapping between NUT and fty-nut is saved in:

```
/usr/share/fty-nut/mapping.conf
```

### State File
State files are located in

```
/var/lib/fty/fty-nut/state_file  (fty-nut)
/var/lib/fty/fty-autoconfig/state (fty-nut-configurator)

```

## Architecture

### Overview

fty-nut is composed of three actors:

* fty_nut_server - server actor
* alert_actor - actor handling device alerts and thresholds
* sensor_actor - actor handling sensors

fty-nut-configurator is composed of 1 actor:

* fty_nut_configurator_server - server actor which configures nut-server (upsd) based on results from nut scanner

## Protocols

### Publishing Metrics

* sensor_actor produces metrics on FTY_PROTO_STREAM_METRICS_SENSOR.
* fty_nut_server produces metrics on FTY_PROTO_STREAM_METRICS.


### Publishing Alerts

* alert_actor produces metrics on FTY_PROTO_STREAM_ALERT_SYS.

### Consuming Assets

* fty_nut_server, sensor_actor and alert_actor listen on FTY_PROTO_STREAM_ASSETS stream.

### Mailbox Requests
No maibox commands implemented
