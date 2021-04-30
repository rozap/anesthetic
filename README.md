# anaesthetic
r a c i n g


## Libraries
* https://github.com/avishorp/TM1637
* https://github.com/rlogiacco/CircularBuffer
* [RadioHead](http://www.airspayce.com/mikem/arduino/RadioHead/) (see setup, below)

## Docs
Oil pressure sensor: autometer 2242 https://www.autometer.com/sensor_specs

## RadioHead setup
RadioHead is the library for the LoRa modules. It runs on Aduino boards.
We use RadioHead 1.116, modified to work on 915MHz (there's a call to setFrequency hardcoded in the library source). A pre-modified version is included in this repo. To setup, unzip `RadioHead-anesthetic.zip` to `$HOME/Arduino/libraries`.

## Radio range test
There is a pair of Arduino sketches for a basic radio range test in `radio_range_test`. See instructions in sketch comments.
