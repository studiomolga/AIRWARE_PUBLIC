import sys
import os
import argparse
import base64
import socket
import datetime
import requests
import schedule
import json
import paho.mqtt.client as mqtt
import paho.mqtt.publish as publish
from paho.mqtt import MQTTException
from requests.exceptions import SSLError
import logging

#TODO: some choices that we need to make at some point will determine our options for dealing with applications adn api keys
TTN_APPLICATIONS = [{'test-app-01-rotterdam': 'NNSXS.R5WETWDQQOUQ64SWECAL3Z4HKM3SKS43KIVOI2Q.QF4PQ5CV3HFKEXZLKEFCODRMKMMQK4N3EBVYFD2RGTJK5AL2TWLA'},
                    {'aware-poc': 'NNSXS.D5GHD6SO44VPDOLNV2QYAENOCIFLWVB4C6NP4VQ.NSMH63X3P55JWUD6NF3MGTQ6KAQ5CMO4MFSA246WZHTS7ZNA6HFA'}]
TTN_MQTT_HOST = 'eu1.cloud.thethings.network'
TTN_MQTT_PORT = 1883

# DOWNLINK_SEND = 0
# DOWNLINK_RECEIVED = 1

BASE_PATH = os.path.dirname(os.path.abspath(__file__))

# logger
logger = logging.getLogger('awair_server')
logger.setLevel(logging.DEBUG)

# console handler
ch = logging.StreamHandler()
ch.setLevel(logging.INFO)

# file handler
fn = os.path.join(BASE_PATH, 'awair_server.log')
fh = logging.FileHandler(fn)
fh.setLevel(logging.DEBUG)

# formatter
formatter_fh = logging.Formatter('[%(asctime)s | %(name)s | %(levelname)s]: %(message)s')
formatter_ch = logging.Formatter('[%(asctime)s | %(name)s | %(levelname)s]: %(message)s')

# add formatter
ch.setFormatter(formatter_ch)
fh.setFormatter(formatter_fh)

# add handlers
logger.addHandler(ch)
logger.addHandler(fh)


class AirQualityLookUp:
    def __init__(self):
        self.pm10 = [0, 17, 34, 51, 59, 67, 76, 84, 92, 101]
        self.pm25 = [0, 12, 24, 36, 42, 47, 54, 59, 65, 71]
        self.no2 = [0, 67, 134, 201, 268, 335, 401, 468, 535, 601]
        self.o3 = [0, 27, 54, 81, 108, 135, 161, 188, 214, 241]

    def _get_ranges(self, pollutant):
        if pollutant == 'pm10':
            return self.pm10
        elif pollutant == 'pm25':
            return self.pm25
        elif pollutant == 'no2':
            return self.no2
        elif pollutant == 'o3':
            return self.o3
        else:
            return None

    def get_daqi_level(self, pollutant, value):
        ranges = self._get_ranges(pollutant)

        if value < 0:
            return 0

        if value >= ranges[-1]:
            return len(ranges) - 1

        for i in range(len(ranges) - 1):
            if ranges[i] <= value < ranges[i + 1]:
                return i


class Location:
    def __init__(self, lon, lat):
        self.lon = lon
        self.lat = lat

    def __repr__(self):
        return f'latitude: {self.lat}, longitude: {self.lon}'


class Device:
    def __init__(self, dev_eui, dev_id, location, aq_lookup, aq_api_key='abcadd63c241ef1e6d56d58cc1a2b0d4'):
        self.dev_eui = dev_eui
        self.dev_id = dev_id
        self.location = location
        self.aq_api_key = aq_api_key
        # self.do_send_downlink = True
        self.air_quality = 0
        self.aq_loookup = aq_lookup
        self._daqi_order = ['no2', 'o3', 'pm10', 'pm25']

    def __repr__(self):
        return f'device eui: {self.dev_eui}, device id: {self.dev_id}, location: {self.location}'

    def set_air_quality(self):
        if self.location is not None:
            timestamp = datetime.datetime.utcnow()
            timestamp = timestamp - datetime.timedelta(hours=1)
            timestamp = timestamp.strftime('%Y-%m-%dT%H:%M:%SZ')
            #print(timestamp)
            url = f'https://swift-exposure.nw.r.appspot.com/exposure/london/coord?key={self.aq_api_key}&lat={self.location.lat}&lng={self.location.lon}&species=no2,o3,pm10,pm25&timestamp={timestamp}&weighted=1'
            
            logger.debug("attempt request from air quality API")
            try:
                response = requests.get(url)
                logger.debug(f'received response: {response}')
                logger.debug(f'received data: {json.dumps(response.json(), indent=4)}')
            except ConnectionError as err:
                logger.error(f'could not connect to air quality api, got error: {err}')
                return
            except SSLError as err:
                logger.error(f'ssl error on air quality api: {err}')
                return

            logger.debug(f'API RESPONSE : {json.dumps(response.json(), indent=4)}')
            #print(json.dumps(response.json(), indent=4))
            data = self.process_aq_data(response.json())
            logger.debug(f'data: {data}')
            if len(data.keys()) == 0:
                return

            #print(data)
            air_quality = 0
            for key in data:
                daqi_level = self.aq_loookup.get_daqi_level(key, data[key])
                offset = self._daqi_order.index(key) * 4
                air_quality += (daqi_level << offset)
                logger.info(f'pollutant: {key}, level: {daqi_level}, bits: {bin(daqi_level)},  total: {air_quality}, bits: {bin(air_quality)}')

            self.air_quality = air_quality

    def process_aq_data(self, data):
        processed_data = {}
        
        results = {}
        try:
            results = data['results']
        except KeyError as err: 
            logger.error(f'key error on air quality return: {err}')
            return processed_data

        for result in results:
            if 'nowcast_value' in result:
                processed_data[result['species']] = result['nowcast_value']

        return processed_data


class Application:
    def __init__(self, app_id, api_key):
        self.app_id = app_id
        self.api_key = api_key
        self.mqttc = mqtt.Client()
        self.mqttc.on_connect = on_connect
        self.mqttc.on_message = on_message
        self.mqttc.on_disconnect = on_disconnect
        self.mqttc.username_pw_set(username=f'{self.app_id}@ttn', password=self.api_key)
        self.mqttc.connect(TTN_MQTT_HOST, TTN_MQTT_PORT, 60)
        # starting like this gives us a thread and handles reconnecting when connection is lost
        self.mqttc.loop_start()
        self.aq_lookup = AirQualityLookUp()

        self.devices = {}

        self.set_devices()
        self.set_air_qualities()

    def set_devices(self):
        url = f'https://eu1.cloud.thethings.network/api/v3/applications/{self.app_id}/devices?field_mask=locations'
        headers = {'Authorization': f'Bearer {self.api_key}',
                   'Accept': 'applicatioin/json',
                   'User-Agent': 'AwairServer/1.0'}

        response = None
        try:
            response = requests.get(url, headers=headers)
        except requests.exceptions.RequestException as err:
            logger.error(f'could not connect to api, received error: {err}')
            return
        #except requests.ConnectionError as err:
        #    logger.error(f'could not connect to api, got error: {err}')
        #    return
        #except SSLError as err:
        #    logger.error(f'ssl error: {err}')
        #    return
        

        if response.status_code == 200:
            data = response.json()
            for device_data in data['end_devices']:
                try:
                    location = Location(device_data['locations']['user']['longitude'],
                                        device_data['locations']['user']['latitude'])
                except KeyError:
                    logger.error(f"device with id: {device_data['ids']['device_id']} did not supply any location data")
                    location = None
                device = Device(device_data['ids']['dev_eui'], device_data['ids']['device_id'], location, self.aq_lookup)
                if device.dev_id not in self.devices or \
                        (self.devices[device.dev_id].location is None and device.location is not None):
                    self.devices[device.dev_id] = device
                    logger.info(f'device: {device}')

    def send_downlinks(self):
        for key in self.devices:
            device = self.devices[key]
            # if device.do_send_downlink:
            payload = base64.b64encode(device.air_quality.to_bytes(length=2, byteorder='little'))
            msg = '{"downlinks": [{"f_port": 15,"frm_payload": "' + payload.decode() + '","priority": "NORMAL"}]}'
            topic = f'v3/{self.app_id}@ttn/devices/{device.dev_id}/down/replace'
            try:
                publish.single(topic, msg, hostname=TTN_MQTT_HOST, port=TTN_MQTT_PORT,
                               auth={'username': f'{self.app_id}@ttn',
                                     'password': self.api_key})
            except socket.timeout as err:
                logger.error(f'socket timed out withh err: {err}, downlink not scheduled')
            except ConnectionRefusedError as err:
                logger.error(f'connection was refused when publishing a downlink on device: {device.dev_id} with error: {err}')
            except socket.gaierror as err:
                logger.error(f'could not publish downlink due to the following error: {err}')
            except MQTTException as err:
                logger.error(f'publish failed with mqtt error: {err}')

    def set_air_qualities(self):
        for key in self.devices:
            logger.debug(f'setting air quality for device: {key}')
            device = self.devices[key]
            device.set_air_quality()


def on_connect(client, userdata, flags, rc):
    logger.info("Connected with result code " + str(rc))

    client.subscribe("#")

def on_disconnect(client, userdata, rc):
    if rc != 0:
        logger.debug(f'unexpected disconnect of mqtt client with code: {rc}')
    else:
        logger.debug(f'expected disconnect of mqtt client with code: {rc}')
    
    client.connect(TTN_MQTT_HOST, TTN_MQTT_PORT, 60)


def on_message(client, userdata, msg):
    topic_parts = msg.topic.split('/')
    app_id = topic_parts[1].split('@')[0]
    dev_id = topic_parts[3]
    msg_type = topic_parts[4]
    # print(topic_parts)

    if msg_type == 'up':
        #logger.('received uplink')
    
        
        payload = json.loads(msg.payload.decode('utf-8'))
        logger.debug(f'incoming message payload: {payload}')
        # payload = int.from_bytes(base64.b64decode(payload), 'big')

        logger.info(f'received uplink from device: {dev_id}')
        # print(payload)
        # if dev_id in applications[app_id].devices:
        #     if payload == DOWNLINK_SEND:
        #         applications[app_id].devices[dev_id].do_send_downlink = True
        #     elif payload == DOWNLINK_RECEIVED:
        #         applications[app_id].devices[dev_id].do_send_downlink = False


def check_devices():
    for key in applications:
        applications[key].set_devices()


def send_downlinks():
    for key in applications:
        applications[key].send_downlinks()


def set_air_qualities():
    logger.debug('running set air qualities')
    for key in applications:
        applications[key].set_air_qualities()


if __name__ == '__main__':
    applications = {}
    for application in TTN_APPLICATIONS:
        for application_id in application:
            application = Application(application_id, application[application_id])
            applications[application_id] = application

    schedule.every().hour.at(':00').do(check_devices)
    schedule.every().hour.at(':30').do(set_air_qualities)
    schedule.every(5).minutes.do(send_downlinks)

    while True:
        schedule.run_pending()

    #use this to stop the threaded loop
    #mqttc.loop_stop(force=False)
