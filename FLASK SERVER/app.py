from flask import Flask, request, send_from_directory, render_template
from flask_socketio import SocketIO
import paho.mqtt.client as mqtt
import paho.mqtt.publish as publish
import os
import csv
import time
import traceback
from datetime import datetime

# ================== Flask + Socket.IO Setup ================== #
app = Flask(__name__, static_folder='images', static_url_path='/images')
socketio = SocketIO(app, cors_allowed_origins="*", async_mode='threading')

UPLOAD_FOLDER = 'uploads'
IMAGE_FOLDER = 'images'
CSV_FILE = 'log.csv'

# Make sure folders exist
os.makedirs(UPLOAD_FOLDER, exist_ok=True)
os.makedirs(IMAGE_FOLDER, exist_ok=True)

# ================== MQTT Setup ================== #
mqtt_broker = '192.168.164.159'
mqtt_port = 1883

mqtt_new_entry = "esp32/new_entry"
mqtt_new_exit = "court/access"
metadata_topic = "camera/metadata"
ack_topic = "camera/ack"

mqtt_client = mqtt.Client()

# ================== Global Variables ================== #
latest_metadata = "default_filename"

# ================== MQTT Callbacks ================== #
def on_connect(client, userdata, flags, rc):
    print("[MQTT] Connected with result code", rc)
    client.subscribe(mqtt_new_entry)
    client.subscribe(mqtt_new_exit)
    client.subscribe(metadata_topic)
    print(f"[MQTT] Subscribed to topics: {mqtt_new_entry}, {mqtt_new_exit}, {metadata_topic}")

def on_message(client, userdata, msg):
    global latest_metadata

    topic = msg.topic
    message = msg.payload.decode()

    print(f"[MQTT] Received message on {topic}: {message}")

    if topic == mqtt_new_entry:
        new_entry_process(message)
    elif topic == mqtt_new_exit:
        new_exit_process(message)
    elif topic == metadata_topic:
        # CORRECTED INDENTATION (USE SPACES, NOT TABS)
        if ',' in message:
            latest_metadata = message.strip()
            print(f"[MQTT] Updated latest metadata to: {latest_metadata}")
        else:
            print(f"[WARNING] Invalid metadata: '{message}'. Expected 'ID,TIME'.")
            
# ================== Camera Image Save and RTI Update ================== #
def camera_process(file_data, metadata):
    print(f"\n[PROCESS] Camera Process Started | Metadata: '{metadata}'")

    try:
        # Split metadata by commas (e.g., "1,17:21:00")
        parts = metadata.split(',')
        if len(parts) < 2:
            raise ValueError(f"Invalid metadata: '{metadata}'. Expected 'ID,TIME'.")

        id_part = parts[0].strip()  # "1"
        timestamp_part = parts[1].strip().replace(':', '-')  # "17-21-00" (safe for filenames)

        filename = f"{id_part}_{timestamp_part}.jpg"
        image_path = os.path.join(UPLOAD_FOLDER, filename)

        with open(image_path, 'wb') as f:
            f.write(file_data)

        print(f"[SUCCESS] Image saved: {filename}")
        publish.single(ack_topic, "image_uploaded", hostname=mqtt_broker)

    except Exception as e:
        print(f"[ERROR] {str(e)}")
        # Fallback to default name if parsing fails
        filename = "default_filename.jpg"
        image_path = os.path.join(UPLOAD_FOLDER, filename)
        with open(image_path, 'wb') as f:
            f.write(file_data)
        print(f"[FALLBACK] Image saved as {filename}")
        publish.single(ack_topic, "upload_error", hostname=mqtt_broker)
                     
# ================== Entry Process ================== #
def new_entry_process(message):
    print("[PROCESS] New Entry Process Started")

    logEntryList = message.split(",")
    rti = logEntryList[14].replace(':', '-')  # RTI is 15th column
    logEntryList[14] = rti
    
    with open(CSV_FILE, 'a', newline='') as csvfile:
        writer = csv.writer(csvfile)
        writer.writerow(logEntryList)

    image_name = logEntryList[6]  # Assuming "Image" is 7th column

    entry_data = {
        "type": "entry",
        "data": {
            "F_ID": logEntryList[0],
            "Name": logEntryList[1],
            "Age": logEntryList[2],
            "Gender": logEntryList[3],
            "DOB": logEntryList[4],
            "A_No": logEntryList[5],
            "Image": image_name,
            "P_No": logEntryList[7],
            "Address": logEntryList[8],
            "RFID": logEntryList[9],
            "Date": logEntryList[10],
            "E_Time": logEntryList[11],
            "Day": logEntryList[13],
            "RTI": logEntryList[14]
        }
    }

    socketio.emit('new_log', entry_data)
    print("Socketio: Send Successful")
    print("[PROCESS] New Entry Process Finished")

    socketio.sleep(20)  # Display for 20 seconds
    socketio.emit('clear_log')
    

# ================== Exit Process ================== #
def new_exit_process(message):
    print("[PROCESS] New Exit Process Started")
    rfid, currentDate, currentTime, currentDay = message.split(",")
    updated_rows = []
    found = False
    updated_row = None  # To store the complete updated row data

    try:
        with open(CSV_FILE, 'r', newline='') as csvfile:
            reader = csv.reader(csvfile)
            try:
                header = next(reader)
            except StopIteration:
                print("[ERROR] log.csv is empty.")
                return

            updated_rows.append(header)

            for row in reader:
                if (len(row) > 13 and
                    row[9] == rfid and
                    row[10] == currentDate and
                    row[13] == currentDay and
                    row[12] == ""):
                    row[12] = currentTime  # Set Ex_Time
                    found = True
                    updated_row = row.copy()  # Store the updated row
                updated_rows.append(row)

        if found:
            # Write updated CSV
            with open(CSV_FILE, 'w', newline='') as csvfile:
                writer = csv.writer(csvfile)
                writer.writerows(updated_rows)

            # Create complete data dictionary
            exit_data = {
                "type": "exit",
                "data": {
                    "F_ID": updated_row[0],
                    "Name": updated_row[1],
                    "Age": updated_row[2],
                    "Gender": updated_row[3],
                    "DOB": updated_row[4],
                    "A_No": updated_row[5],
                    "Image": updated_row[6],
                    "P_No": updated_row[7],
                    "Address": updated_row[8],
                    "RFID": updated_row[9],
                    "Date": updated_row[10],
                    "E_Time": updated_row[11],
                    "Ex_Time": updated_row[12],
                    "Day": updated_row[13],
                    "RTI": updated_row[14] if len(updated_row) > 14 else ""
                }
            }

            socketio.emit('exit_update', exit_data)
            print("Socketio: Send Successful")
            socketio.sleep(5)
            socketio.emit('clear_log')
            print("[PROCESS] New Exit Process Finished")
        else:
            print("[PROCESS] No matching row found or exit already marked.")

    except Exception as e:
        print(f"[ERROR] During exit process: {e}")
        traceback.print_exc()

mqtt_client.on_connect = on_connect
mqtt_client.on_message = on_message
mqtt_client.connect(mqtt_broker, mqtt_port, 60)
mqtt_client.loop_start()

# ================== Flask Routes ================== #
@app.route('/')
def index():
    return render_template('index.html')

@app.route('/upload', methods=['POST'])
def upload_image():
    global latest_metadata

    file_data = request.data
    if file_data:
       # filename = f"{latest_metadata}.jpg"
        camera_process(file_data, latest_metadata)
        return "Image Received", 200

    return "No File", 400

@app.route('/uploads/<filename>')
def get_uploaded_image(filename):
    return send_from_directory(UPLOAD_FOLDER, filename)

@app.route('/images/<filename>')
def get_image(filename):
    return send_from_directory(IMAGE_FOLDER, filename)

# ================== Start Server ================== #
if __name__ == '__main__':
    
    socketio.run(app, host='0.0.0.0', port=5000)
