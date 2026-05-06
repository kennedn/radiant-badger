# radiant-badger
Interface for controlling smart radiators and thermostats on the Badger 2040 W

![](./media/demo.gif)

## LiPo Charge Mod

The Badger 2040 W does not come with charge circuitry for LiPo batteries, a TP4056 module was soldered in situ to rectify this. The Badger 2040 W isolates the LiPo JST and USB power via a mosfet making this mod feasible.

Parts:
- TP4056 module (usb desoldered)
- Lipo Battery 500mAh (403048)


<p align="center">
    <img src="./media/TP4056.JPG" width="48%"/>
    <img src="./media/assembly.JPG" width="48%"/>
</p>


# Preparing the build environment

Install build requirements:

```shell
sudo apt update
sudo apt install cmake gcc-arm-none-eabi build-essential
```

Install the Pico SDK:

```shell 
git clone -b master https://github.com/raspberrypi/pico-sdk.git --recursive
export PICO_SDK_PATH="$(pwd)/pico-sdk"
```

The `PICO_SDK_PATH` set above will only last the duration of your session.

You should should ensure your `PICO_SDK_PATH` environment variable is set in your profile, e.g `~/.bash_profile`

```shell
export PICO_SDK_PATH="/path/to/pico-sdk"
```

## Grab my fork of the Pimoroni libraries

```shell
git clone -b badger-2040w https://github.com/kennedn/pimoroni-pico /path/to/pimoroni-pico
export PIMORONI_PICO_PATH="/path/to/pimoroni-pico"
```

# Configuration

> NOTE: As much WIFI information as possible is specified at compile time in an effort to reduce wifi connect times since these occur each time the device wakes up

The following definitions are required to build the project:

| Definition          | Description                                    |
|---------------------|------------------------------------------------|
| WIFI_SSID           | Name of wifi network to join                   |
| WIFI_PASSWORD       | Password of wifi network to join               |
| WIFI_BSSID          | BSSID of wifi network to join                  |
| WIFI_CHANNEL        | Channel of wifi network to join                |
| API_SERVER          | API server to use for HTTP calls               |

Additional definitions have default values that can be overridden:

| Definition          | Default value  | Description                            |
|---------------------|----------------|----------------------------------------|
| NTP_SERVER          | pool.ntp.org   | NTP server to retrieve time from       |
| IP_ADDRESS          | 192.168.1.203  | Static IP address to use on network    |
| IP_GATEWAY          | 192.168.1.1    | Default gateway to use on network      |
| IP_DNS              | 192.168.1.1    | DNS address to use for name resolution |
| JSON_FILEPATH       | PROJECT_ROOT/config/tiles.json | JSON file containing tile definitions |
| DEBUG_PRINT         | 0              | Enables debug printing to (USB) UART   |

> NOTE: The Pico SDK also requires PICO_BOARD be set to pico_w to build wifi projects

## JSON file

A JSON file is required to define columns and tiles in radiant-badger. It's default location is `PROJECT_ROOT/config/tiles.json`.

The file contains an array of columns. Each column contains up to 3 tiles (tile_a, tile_b, tile_c) that are displayed on the screen, and can be navigated with UP/DOWN buttons.

### Column

| Key      | Description                                                   |
|----------|---------------------------------------------------------------|
| heading  | Display name for the column (e.g., "UP", "DOWN")              |
| icon_idx | Index of icon to display from [image_tiles[]](/src/images.h#54) |
| tile_a   | Left tile definition (required)                               |
| tile_b   | Center tile definition (optional)                             |
| tile_c   | Right tile definition (optional)                              |

### Tile

| Key                     | Type   | Description                                                      |
|-------------------------|--------|------------------------------------------------------------------|
| name                    | string | Tile name, shown under the tile icon                             |
| type                    | number | 0 = BOILER, 1 = RADIATOR                                         |
| image_idx               | number | Index of icon to display                                         |
| status_request          | object | HTTP request to fetch current tile status                        |
| mode_request            | object | **Optional** HTTP request to change mode (BOILER only)           |
| target_request          | object | **Optional** HTTP request to change target temp (BOILER only)    |
| battery_request         | object | **Optional** HTTP request to fetch battery (RADIATOR only)       |
| boost_request           | object | **Optional** HTTP request to set boost (RADIATOR only)           |
| schedule_request        | object | **Optional** HTTP request to change schedule (BOILER only)       |
| schedule_status_request | object | **Optional** HTTP request to fetch schedule (BOILER only)        |

### HTTP Request

| Key       | Description                                         |
|-----------|-----------------------------------------------------|
| method    | HTTP method (POST or GET)                           |
| endpoint  | HTTP URL endpoint, appended to API_SERVER           |
| json_body | JSON string with optional %d or %s format specifiers|
| schedules | Optional array of schedule names                    |

#### Example Column

```json
{
    "heading": "UP",
    "icon_idx": 4,
    "tile_a": {
        "name": "BOILER",
        "type": 0,
        "image_idx": 13,
        "status_request": {
            "method": "POST",
            "endpoint": "/v2/thermostat",
            "json_body": "{\"code\": \"status\"}"
        },
        "boost_request": {
            "method": "POST",
            "endpoint": "/v2/thermostat",
            "json_body": "{\"code\": \"boost\", \"value\":\"%d\"}"
        }
    },
    "tile_b": {
        "name": "OFFICE",
        "type": 1,
        "image_idx": 14,
        "status_request": {
            "method": "POST",
            "endpoint": "/v2/radiator/office",
            "json_body": "{\"code\": \"status\"}"
        },
        "boost_request": {
            "method": "POST",
            "endpoint": "/v2/radiator/office",
            "json_body": "{\"code\": \"boost\", \"value\":\"%d\"}"
        }
    }
}
```

# Building 

Clone Repo and cd:

```bash
git clone https://github.com/kennedn/radiant-badger
cd radiant-badger
```
Configure the JSON file at `PROJECT_ROOT/config/tiles.json`

```bash
cd config
vi tiles.json
cd ..
```

> NOTE: If **tiles.json** is updated in the future, `cmake ..` must be run from the build directory again to apply any changes

Make build directory and cd:

```bash
mkdir build
cd build
```


Run cmake with definitions:

```bash
cmake .. \
  -DPICO_BOARD=pico_w \
  -DWIFI_SSID="cool-wifi-ssid" \
  -DWIFI_PASSWORD="cool-wifi-password" \
  -DWIFI_BSSID=66:55:44:33:22:11 \
  -DWIFI_CHANNEL=9 \
  -DAPI_SERVER="api.cool.com" \
  -DPICO_SDK_PATH="/path/to/pico-sdk" \
  -DPIMORONI_PICO_PATH="/path/to/pimoroni-pico" \
  -DCMAKE_BUILD_TYPE=Release
```

Cd to src folder and make:

```bash
cd src
make
```
