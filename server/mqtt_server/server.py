import argparse
import base64
import socket

import requests
import schedule
import json
import paho.mqtt.client as mqtt
import paho.mqtt.publish as publish
from requests.exceptions import SSLError

#TODO: some choices that we need to make at some point will determine our options for dealing with applications adn api keys
TTN_APPLICATIONS = [{'test-app-01-rotterdam': 'NNSXS.R5WETWDQQOUQ64SWECAL3Z4HKM3SKS43KIVOI2Q.QF4PQ5CV3HFKEXZLKEFCODRMKMMQK4N3EBVYFD2RGTJK5AL2TWLA'}]
TTN_MQTT_HOST = 'eu1.cloud.thethings.network'
TTN_MQTT_PORT = 1883

# DOWNLINK_SEND = 0
# DOWNLINK_RECEIVED = 1


class AirQualityLookUp:
    def __init__(self):
        self.pm10 = [0, 51, 76, 101]
        self.pm25 = [0, 36, 54, 71]
        self.no2 = [0, 201, 401, 601]
        self.o3 = [0, 81, 161, 241]

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
    def __init__(self, dev_eui, dev_id, location, aq_lookup, aq_api_key='f3f1005e95f2b68a91b1e2ff2882481b'):
        self.dev_eui = dev_eui
        self.dev_id = dev_id
        self.location = location
        self.aq_api_key = aq_api_key
        self.do_send_downlink = True
        self.air_quality = 0
        self.aq_loookup = aq_lookup
        self._daqi_order = ['no2', 'o3', 'pm10', 'pm25']

    def __repr__(self):
        return f'device eui: {self.dev_eui}, device id: {self.dev_id}, location: {self.location}'

    def set_air_quality(self):
        if self.location is not None:
            url = f'https://swift-exposure.nw.r.appspot.com/exposure/london/coord?key={self.aq_api_key}&lat={self.location.lat}&lng={self.location.lon}&species=no2,o3,pm10,pm25&weighted=1'

            try:
                response = requests.get(url)
            except ConnectionError as err:
                print(f'could not connect to air quality api, got error: {err}')
                return
            except SSLError as err:
                print(f'ssl error on air quality api: {err}')
                return

            # print(json.dumps(response.json(), indent=4))
            data = self.process_aq_data(response.json())
            print(data)
            air_quality = 0
            for key in data:
                daqi_level = self.aq_loookup.get_daqi_level(key, data[key])
                offset = self._daqi_order.index(key) * 2
                air_quality += (daqi_level << offset)
                print(f'pollutant: {key}, level: {daqi_level}, bits: {bin(daqi_level)},  total: {air_quality}, bits: {bin(air_quality)}')

            self.air_quality = air_quality

    def process_aq_data(self, data):
        results = data['results']
        processed_data = {}

        for result in results:
            processed_data[result['species']] = result['value']

        return processed_data


class Application:
    def __init__(self, app_id, api_key):
        self.app_id = app_id
        self.api_key = api_key
        self.mqttc = mqtt.Client()
        self.mqttc.on_connect = on_connect
        self.mqttc.on_message = on_message
        self.mqttc.username_pw_set(username=f'{self.app_id}@ttn', password=self.api_key)
        self.mqttc.connect(TTN_MQTT_HOST, TTN_MQTT_PORT, 60)
        # starting like this gives us a thread and handles reconnecting when connection is lost
        self.mqttc.loop_start()
        self.aq_lookup = AirQualityLookUp()

        self.devices = {}

    def set_devices(self):
        url = f'https://eu1.cloud.thethings.network/api/v3/applications/{self.app_id}/devices?field_mask=locations'
        headers = {'Authorization': f'Bearer {self.api_key}',
                   'Accept': 'applicatioin/json',
                   'User-Agent': 'AwairServer/1.0'}

        response = None
        try:
            response = requests.get(url, headers=headers)
        except ConnectionError as err:
            print(f'could not connect to api, got error: {err}')
            return
        except SSLError as err:
            print(f'ssl error: {err}')
            return

        if response.status_code == 200:
            data = response.json()
            for device_data in data['end_devices']:
                try:
                    location = Location(device_data['locations']['user']['longitude'],
                                        device_data['locations']['user']['latitude'])
                except KeyError:
                    print(f"device with id: {device_data['ids']['device_id']} did not supply any location data")
                    location = None
                device = Device(device_data['ids']['dev_eui'], device_data['ids']['device_id'], location, self.aq_lookup)
                if device.dev_id not in self.devices or \
                        (self.devices[device.dev_id].location is None and device.location is not None):
                    self.devices[device.dev_id] = device
                    print(device)

    def send_downlinks(self):
        for key in self.devices:
            device = self.devices[key]
            if device.do_send_downlink:
                payload = base64.b64encode(device.air_quality.to_bytes(length=1, byteorder='little'))
                msg = '{"downlinks": [{"f_port": 15,"frm_payload": "' + payload.decode() + '","priority": "NORMAL"}]}'
                topic = f'v3/{self.app_id}@ttn/devices/{device.dev_id}/down/replace'
                try:
                    publish.single(topic, msg, hostname=TTN_MQTT_HOST, port=TTN_MQTT_PORT,
                                   auth={'username': f'{self.app_id}@ttn',
                                         'password': self.api_key})
                except socket.timeout as err:
                    print(f'socket timedout withh err: {err}, downlink not scheduled')

    def set_air_qualities(self):
        for key in self.devices:
            device = self.devices[key]
            device.set_air_quality()


def on_connect(client, userdata, flags, rc):
    print("Connected with result code " + str(rc))

    client.subscribe("#")


def on_message(client, userdata, msg):
    topic_parts = msg.topic.split('/')
    app_id = topic_parts[1].split('@')[0]
    dev_id = topic_parts[3]
    msg_type = topic_parts[4]
    # print(topic_parts)

    if msg_type == 'up':
        print('received uplink')

        payload = json.loads(msg.payload.decode('utf-8'))['uplink_message']['frm_payload']
        payload = int.from_bytes(base64.b64decode(payload), 'big')

        print(f'received uplink from device: {dev_id} with value: {payload}')
        # print(payload)
        # if dev_id in applications[app_id].devices:
        #     if payload == DOWNLINK_SEND:
        #         applications[app_id].devices[dev_id].do_send_downlink = True
        #     elif payload == DOWNLINK_RECEIVED:
        #         applications[app_id].devices[dev_id].do_send_downlink = False


def check_devices(apps):
    # print('checking for new devices')
    for key in apps:
        apps[key].set_devices()


def send_downlinks():
    for key in applications:
        applications[key].send_downlinks()


def set_air_qualities():
    for key in applications:
        applications[key].set_air_qualities()


if __name__ == '__main__':
    applications = {}
    for application in TTN_APPLICATIONS:
        for application_id in application:
            application = Application(application_id, application[application_id])
            applications[application_id] = application

    schedule.every().hour.at(':00').do(check_devices, applications)
    schedule.every().hour.at(':00').do(set_air_qualities)
    schedule.every(5).minutes.do(send_downlinks)

    while True:
        schedule.run_pending()

    #use this to stop the threaded loop
    #mqttc.loop_stop(force=False)
