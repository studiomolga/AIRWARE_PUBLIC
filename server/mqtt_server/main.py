import json
import base64
import requests
import paho.mqtt.client as mqtt
import paho.mqtt.publish as publish
import random


TTN_USERNAME = 'lift-test@ttn'

NODE_DB = {'eui-9876b6000011b613': {'id': 'eui-9876b6000011b613', 'lat': 51.497961, 'lng': -0.176515, 'key': 'f3f1005e95f2b68a91b1e2ff2882481b'}}

QUALITY_LOW = 0x00
QUALITY_NORMAL = 0x01
QUALITY_HIGH = 0x02

# TODO: the below line is verified working, check difference between this code and that link...
#  if there is no difference just implement a version with this:
#  publish.single("v3/lift-test@ttn/devices/eui-9876b6000011b613/down/push", '{"downlinks":[{"f_port": 15,"frm_payload":"AA==","priority": "NORMAL"}]}', hostname="eu1.cloud.thethings.network", port=1883, auth={'username':"lift-test@ttn",'password':"NNSXS.AKSZXONGQIA6TOMFBTWOPU7CXAUF4LB4XEIWX7I.ONYC4Y36HXHFSG7Q66WYW2ILWMQ3ARJZGCL73M5YU5FV72DESUVA"})


# The callback for when the client receives a CONNACK response from the server.
def on_connect(client, userdata, flags, rc):
    print("Connected with result code "+str(rc))

    # Subscribing in on_connect() means that if we lose the connection and
    # reconnect then subscriptions will be renewed.
    client.subscribe("#")


# The callback for when a PUBLISH message is received from the server.
def on_message(client, userdata, msg):
    print(msg.topic)
    json_str = msg.payload.decode('utf-8')
    data = json.loads(json_str)
    #print(json.dumps(data, indent=4))

    if msg.topic.endswith('up'):
        process_uplink(data)


def validate_uplink(data):
    print(data)
    if data['end_device_ids']['device_id']:
        return True

    return False


def process_uplink(data):
    if validate_uplink(data):
        #node_id = data['uplink_message']['decoded_payload']['bytes'][0]
        device_id = data['end_device_ids']['device_id']
    else:
        return 0

    # print(node_id)

    # here should now come a call to a database with all devices and their important api settings, for now it will be faked
    # could be that we end up using some cli program ttn offers to make inqueries to our account for getting devices and
    # then subscribing to their mqtt topics
    api_data = NODE_DB[device_id]
    api_url = f'https://swift-exposure.nw.r.appspot.com/exposure/london/coord?key={api_data["key"]}&lat={api_data["lat"]}&lng={api_data["lng"]}&species=no2,o3,pm10,pm25&weighted=1'

    response = requests.get(api_url)

    if response.status_code == 200:
        res_data = response.json()
        no2 = res_data['results'][0]['value']
        o3 = res_data['results'][1]['value']
        pm10 = res_data['results'][2]['value']
        pm25 = res_data['results'][3]['value']
        # print(response.json())

        # here should following some logic for determining the quality of the air, we will fake this for now
        air_qualities = bytearray([QUALITY_NORMAL, QUALITY_HIGH, QUALITY_LOW, QUALITY_NORMAL])
        random.shuffle(air_qualities)
        print(air_qualities)

        b64_data = base64.b64encode(air_qualities)
        # print(b64_data)

        msg = '{"downlinks": [{"f_port": 15,"frm_payload": "' + b64_data.decode() + '","priority": "HIGH"}]}'
        # print(msg)
        topic = f'v3/{TTN_USERNAME}/devices/{device_id}/down/push'
        # print(topic)

        publish.single("v3/lift-test@ttn/devices/eui-9876b6000011b613/down/replace",
                       msg,
                       hostname="eu1.cloud.thethings.network", port=1883, auth={'username': "lift-test@ttn",
                                                                                'password': "NNSXS.AKSZXONGQIA6TOMFBTWOPU7CXAUF4LB4XEIWX7I.ONYC4Y36HXHFSG7Q66WYW2ILWMQ3ARJZGCL73M5YU5FV72DESUVA"})

        # print(client.publish(topic, msg))

    # print(device_id, TTN_USERNAME)


client = mqtt.Client()
client.on_connect = on_connect
client.on_message = on_message

client.username_pw_set(username='lift-test@ttn', password='NNSXS.AKSZXONGQIA6TOMFBTWOPU7CXAUF4LB4XEIWX7I.ONYC4Y36HXHFSG7Q66WYW2ILWMQ3ARJZGCL73M5YU5FV72DESUVA')
client.connect("eu1.cloud.thethings.network", 1883, 60)

# Blocking call that processes network traffic, dispatches callbacks and
# handles reconnecting.
# Other loop*() functions are available that give a threaded interface and a
# manual interface.
client.loop_forever()
