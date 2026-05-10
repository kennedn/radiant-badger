# radiant-badger
Interface for controlling smart radiators, thermostats, and RESTful smart devices on the Badger 2040 W.

![](./media/demo2.gif)

## LiPo Charge Mod

The Badger 2040 W does not come with charge circuitry for LiPo batteries, a TP4056 module was soldered in situ to rectify this. The Badger 2040 W isolates the LiPo JST and USB power via a mosfet making this mod feasible.

Parts:
- TP4056 module (usb desoldered)
- Lipo Battery 500mAh (403048)


<p align="center">
    <img src="./media/TP4056.JPG" width="48%"/>
    <img src="./media/assembly.JPG" width="48%"/>
</p>


## Preparing the build environment

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

## Configuration

> NOTE: DNS lookup is skipped if `API_IP` is provided, this provides a significant speedup for initial http request
The following definitions are required to build the project:

| Definition    | Description                              |
|---------------|------------------------------------------|

Additional definitions have default values that can be overridden:

| Definition    | Default value                  | Description                                     |
|---------------|--------------------------------|-------------------------------------------------|
| WIFI_SSID     | unset                          | Name of wifi network to join                    |
| WIFI_PASSWORD | unset                          | Password of wifi network to join                |
| API_SERVER    | unset                          | API server host name used for HTTP calls        |
| API_IP        | unset                          | Optional pre-resolved IP for API requests       |
| NTP_SERVER    | pool.ntp.org                   | NTP server to retrieve time from                |
| TZ            | GMT0BST,M3.5.0/1,M10.5.0/2     | POSIX Timezone string used time display         |
| JSON_FILEPATH | PROJECT_ROOT/config/tiles.json | JSON file containing tile definitions           |
| DEBUG_PRINT   | 0                              | Enables debug printing to (USB) UART            |

> NOTE: The Pico SDK also requires PICO_BOARD be set to pico_w to build wifi projects

## JSON file

A JSON file is required to define columns and tiles in radiant-badger. Its default location is `PROJECT_ROOT/config/tiles.json`.

The file contains an array of columns. Each column contains up to 3 tiles (`tile_a`, `tile_b`, `tile_c`) that are displayed on the screen and can be navigated with the UP/DOWN buttons. 

### Column

| Key      | Description                                                     |
|----------|-----------------------------------------------------------------|
| heading  | Display name for the column (e.g. "UP", "DOWN")              |
| icon_idx | Index of icon to display from [image_tiles[]](/src/modules/images.h#148) |
| tile_a   | Left tile definition (optional)                                 |
| tile_b   | Center tile definition (optional)                               |
| tile_c   | Right tile definition (optional)                                |

### Boiler tile

| Key | Type | Description |
| --- | --- | --- |
| name | string | Tile name, shown under the tile icon |
| type | number | 0 = BOILER |
| image_idx | number | Index of icon to display |
| status_request | object | HTTP request to fetch current tile status |
| mode_request | object | Optional HTTP request to change mode |
| target_request | object | Optional HTTP request to change target temp |
| schedule_request | object | Optional HTTP request to change schedule |
| schedule_status_request | object | Optional HTTP request to fetch schedule |

### Radiator tile

| Key | Type | Description |
| --- | --- | --- |
| name | string | Tile name, shown under the tile icon |
| type | number | 1 = RADIATOR |
| image_idx | number | Index of icon to display |
| status_request | object | HTTP request to fetch current tile status |
| mode_request | object | Optional HTTP request to change mode |
| battery_request | object | Optional HTTP request to fetch battery status |
| boost_request | object | Optional HTTP request to set boost |

### RESTFUL tile

| Key | Type | Description |
| --- | --- | --- |
| name | string | Tile name, shown under the tile icon |
| type | number | 2 = RESTFUL |
| image_idx | number | Index of icon to display |
| action_request | object | HTTP request sent when the tile is activated |
| status_request | object | HTTP request to fetch the post-action status |
| status_key | string | JSON key to extract from the response body |
| status_on_value | string | Value that should render as ON |
| status_off_value | string | Value that should render as OFF |

### HTTP Request

| Key       | Description                                          |
|-----------|------------------------------------------------------|
| method    | HTTP method (POST or GET)                            |
| endpoint  | HTTP URL endpoint, appended to API_SERVER            |
| json_body | JSON string with optional %d or %s format specifiers |

#### Example file

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
        "mode_request": {
            "method": "POST",
            "endpoint": "/v2/thermostat",
            "json_body": "{\"code\": \"mode\",\"value\":\"%d\"}"
        },
        "target_request": {
            "method": "POST",
            "endpoint": "/v2/thermostat",
            "json_body": "{\"code\":\"heatTemp\",\"value\":%d}"
        },
        "boost_request": {
            "method": "POST",
            "endpoint": "/v2/thermostat",
            "json_body": "{\"code\": \"boost\", \"value\":\"%d\"}"
        },
        "schedule_request": {
            "method": "POST",
            "endpoint": "/v2/radiator",
            "json_body": "{\"hosts\":\"office,bedroom,kitchen,livingroom\",\"code\":\"schedule\",\"value\":\"%s\"}",
            "schedules": ["default", "study"]
        },
        "schedule_status_request": {
            "method": "POST",
            "endpoint": "/v2/radiator",
            "json_body": "{\"hosts\":\"office,bedroom,kitchen,livingroom\",\"code\":\"status\"}"
        }
    },
    "tile_b": {
        "name": "LAMP",
        "type": 2,
        "image_idx": 0,
        "action_request": {
            "method": "POST",
            "endpoint": "/v2/meross/lamp",
            "json_body": "{\"code\": \"toggle\"}"
        },
        "status_request": {
            "method": "POST",
            "endpoint": "/v2/meross/lamp",
            "json_body": "{\"code\": \"status\"}",
            "key": "onoff",
            "on_value": "1",
            "off_value": "0"
        }
    }
}
```

## Building

Clone Repo and cd:

```bash
git clone https://github.com/kennedn/radiant-badger
cd radiant-badger
```
Configure the JSON file at `PROJECT_ROOT/config/tiles.json`

```bash
vi config/tiles.json
```

> NOTE: If **tiles.json** is updated in the future, CMake must be run again so the generated tile data is refreshed.

Configure and build:

```bash
cmake -B build \
    -DPICO_BOARD=pico_w \
    -DPICO_PLATFORM=rp2040 \
    -DWIFI_SSID="YOUR_WIFI_SSID" \
    -DWIFI_PASSWORD="YOUR_WIFI_PASSWORD" \
    -DAPI_SERVER="YOUR_API_SERVER" \
    -DAPI_IP="YOUR_API_IP_OPTIONAL" \
    -DPICO_SDK_PATH="/path/to/pico-sdk" \
    -DPIMORONI_PICO_PATH="/path/to/pimoroni-pico" 

cmake --build build -j $(nproc)
```