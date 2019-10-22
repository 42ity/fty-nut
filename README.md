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
./src/fty-nut --mapping-file <path_to_mapping_file>
./src/fty-nut --mapping-file /usr/share/fty-common-nut/mapping.conf

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
/usr/share/fty-common-nut/mapping.conf
```

### State File
The fty-nut-configurator state file is located in

```
/var/lib/fty/fty-autoconfig/state

```

## Architecture

### Overview

fty-nut is composed of three actors:

* fty_nut_server - server actor
* alert_actor - actor handling device alerts and thresholds coming from NUT
* sensor_actor - actor handling sensor measurements coming from NUT.

fty-nut-configurator is composed of 1 actor:

* fty_nut_configurator_server - server actor which configures nut-server (upsd) based on results from nut scanner

fty-nut-command is composed of 1 actor:

* fty-nut-command - bridge forwarding commands from Malamute to NUT devices

## Protocols

### Publishing Metrics

* sensor_actor produces metrics from sensors connected to power devices on FTY_PROTO_STREAM_METRICS_SENSOR and fty_shm.

```
stream=_METRICS_SENSOR
sender=agent-nut-sensor
subject=humidity.0@epdu-77
D: 17-11-02 12:18:16 FTY_PROTO_METRIC:
D: 17-11-02 12:18:16     aux=
D: 17-11-02 12:18:16         port=0
D: 17-11-02 12:18:16     time=1509625096
D: 17-11-02 12:18:16     ttl=60
D: 17-11-02 12:18:16     type='humidity.0'
D: 17-11-02 12:18:16     name='epdu-77'
D: 17-11-02 12:18:16     value='37.60'
D: 17-11-02 12:18:16     unit='%'
```

```
stream=_METRICS_SENSOR
sender=agent-nut-sensor
subject=status.GPI1.0@epdu-76
D: 17-11-13 15:21:57 FTY_PROTO_METRIC:
D: 17-11-13 15:21:57     aux=
D: 17-11-13 15:21:57         sname=sensorgpio-81
D: 17-11-13 15:21:57         port=0
D: 17-11-13 15:21:57         ext-port=1
D: 17-11-13 15:21:57     time=1510586517
D: 17-11-13 15:21:57     ttl=60
D: 17-11-13 15:21:57     type='status.GPI1.0'
D: 17-11-13 15:21:57     name='epdu-76'
D: 17-11-13 15:21:57     value='closed'
D: 17-11-13 15:21:57     unit=''
```
alerts for sensors are managed by fty-alert-engine (environmental sensors) and fty-alert-flexible (GPI sensors)

* fty_nut_server produces metrics on FTY_PROTO_STREAM_METRICS and fty_shm.

```
stream=METRICS
sender=fty-nut
subject=status.outlet.2@ups-52
D: 17-11-13 12:53:21 FTY_PROTO_METRIC:
D: 17-11-13 12:53:21     aux=
D: 17-11-13 12:53:21     time=1510577601
D: 17-11-13 12:53:21     ttl=60
D: 17-11-13 12:53:21     type='status.outlet.2'
D: 17-11-13 12:53:21     name='ups-52'
D: 17-11-13 12:53:21     value='42'
D: 17-11-13 12:53:21     unit=''
```

* fty-nut-command doesn't produce metrics.

### Publishing Alerts

* alert_actor produces metrics on FTY_PROTO_STREAM_ALERT_SYS.

```
stream=_ALERTS_SYS
sender=bios-nut-alert
subject=outlet.group.1.voltage@epdu-54/OKG@epdu-54
D: 17-11-13 15:05:57 FTY_PROTO_ALERT:
D: 17-11-13 15:05:57     aux=
D: 17-11-13 15:05:57     time=1510560758
D: 17-11-13 15:05:57     ttl=90
D: 17-11-13 15:05:57     rule='outlet.group.1.voltage@epdu-54'
D: 17-11-13 15:05:57     name='epdu-54'
D: 17-11-13 15:05:57     state='RESOLVED'
D: 17-11-13 15:05:57     severity='OK'
D: 17-11-13 15:05:57     description='outlet.group.1.voltage is resolved'
D: 17-11-13 15:05:57     action=''
```

* fty-nut-command doesn't produce alerts.

### Consuming Assets

* fty_nut_server, sensor_actor and alert_actor listen on FTY_PROTO_STREAM_ASSETS stream.
* fty-nut-command doesn't consume assets.

### Mailbox Requests

fty-nut-command responds to messages with subject `power-actions`. It is
important to note that fty-nut-command hides the details of daisy-chained NUT
devices to its clients by offering an uniform interface across daisy-chained
devices and their hosts.

The following request queries all known commands of an asset:
* "GET_COMMANDS"/'uuid'/('assetN')*...

where
* '/' indicates a multipart string message
* 'uuid' is a client-provided string returned in the reply
* 'assetN' is a list of asset names, one per message part

fty-nut-command will respond back with either:
* "OK"/'uuid'/("ASSET"/'assetN'/('commandN'/'descriptionN')\*)\*
* "ERROR"/'uuid'/'reason'

where
* '/' indicates a multipart string message
* 'uuid' is the client-provided string in the request
* "ASSET" is a constant delimiter string
* 'assetN' is an internal asset name
* 'commandN' is a command provided by 'assetN'
* 'descriptionN' is the description of 'commandN'

The following request performs one or more actions on an asset:
* "DO_NATIVE_COMMANDS"/'uuid'/'asset'/('commandN'/'argumentN')*

where
* '/' indicates a multipart string message
* 'uuid' is the client-provided string in the request
* 'asset' is an internal asset name
* 'commandN' is a command for the asset
* 'argumentN' is an argument for 'commandN', set to empty string if not used

fty-nut-command will respond back with either:
* "OK"/'uuid'
* "ERROR"/'uuid'/'reason'
