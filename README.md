# harbour-slackfish

Unoffical [Slack](https://slack.com/) client for [Sailfish](https://sailfishos.org) available in _Jolla Store_. See Slackfish in [Slack App Directory](https://slack.com/apps/A8LHPP1MK-slackfish)

## Development

* Install [qpm](https://www.qpm.io/)
* download dependencies ([Qt AsyncFuture](https://github.com/benlau/asyncfuture))
```
qpm install
```

* Create [new Slack application](https://api.slack.com/apps?new_app=1) 
and insert its id and secret to file `/vendor/vendor.pri`:
```pri
slack_client_id=xxxxxxxxxxx.xxxxxxxxxxxx
slack_client_secret=xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx
``` 

* Connect to Mer SDK via ssh and, go to Slackfish project directory and build it:
```bash
mb2 -t SailfishOS-2.1.4.13-armv7hl build
```

