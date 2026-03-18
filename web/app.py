from flask import Flask, render_template, jsonify
import serial
import threading
import json
from datetime import datetime
import time

app = Flask(__name__)

# Global latest data for all nodes
latest = {
    "node1": {"temp_C": 0.0, "humidity": 0.0, "seq": 0, "time": "Waiting...", "fault_rate": 0},
    "node2": {"temp_C": 0.0, "humidity": 0.0, "seq": 0, "time": "Waiting...", "fault_rate": 0},
    "node3": {"temp_C": 0.0, "humidity": 0.0, "seq": 0, "time": "Waiting...", "fault_rate": 0}    #value is added 
}

import collections
# Keep track of history for all nodes
history_node1 = collections.deque(maxlen=10)
history_node2 = collections.deque(maxlen=10)
history_node3 = collections.deque(maxlen=10)

# Simulate fault rate
fault_count = {"node1": 0, "node2": 0, "node3": 0}
total_packets = {"node1": 0, "node2": 0, "node3": 0}

# Change this to COM16 for the physical receiver board
SERIAL_PORT = "COM16"

def read_serial():
    global latest
    while True:
        try:
            print(f"🔄 Connecting to {SERIAL_PORT}...")
            # Use serial_for_url for the RFC2217 protocol
            ser = serial.serial_for_url(SERIAL_PORT, baudrate=115200, timeout=1)
            print("✅ Web Dashboard connected to Wokwi!")
            
            while True:
                line = ser.readline().decode('utf-8', errors='ignore').strip()
                if line:
                    print(f"📡 Received: {line}")
                    
                    # Parse Node 2 (Chaotic Secure)
                    if "Node 2 Chaotic Paylaod:" in line:
                        try:
                            start = line.find("{")
                            end = line.rfind("}") + 1
                            data = json.loads(line[start:end])
                            
                            node2_data = {
                                "temp_C": data.get("temp_C", data.get("t", 0)),
                                "humidity": data.get("humidity", data.get("h", 0)),
                                "seq": data.get("seq", 0),
                                "time": datetime.now().strftime("%I:%M:%S %p")
                            }
                            
                            total_packets["node2"] += 1
                            if node2_data["temp_C"] == 0 and node2_data["humidity"] == 0:
                                fault_count["node2"] += 1
                                
                            node2_data["fault_rate"] = round((fault_count["node2"] / total_packets["node2"]) * 100, 1) if total_packets["node2"] > 0 else 0
                            
                            latest["node2"] = node2_data
                            history_node2.appendleft(node2_data)
                        except:
                            pass
                            
                    # Parse Node 1 and Node 3 (AES-GCM Secure)
                    elif "[RX] SECURE DATA Node" in line:
                        import re
                        match = re.search(r'Node (\d+): T=([\d.-]+) C, H=([\d.-]+) %, Seq=(\d+)', line)
                        if match:
                            nid = match.group(1)
                            node_key = f"node{nid}"
                            
                            node_data = {
                                "temp_C": float(match.group(2)),
                                "humidity": float(match.group(3)),
                                "seq": int(match.group(4)),
                                "time": datetime.now().strftime("%I:%M:%S %p")
                            }
                            
                            if node_key not in total_packets:
                                total_packets[node_key] = 0
                                fault_count[node_key] = 0

                            total_packets[node_key] += 1
                            if node_data["temp_C"] == 0 and node_data["humidity"] == 0:
                                fault_count[node_key] += 1
                                
                            node_data["fault_rate"] = round((fault_count[node_key] / total_packets[node_key]) * 100, 1) if total_packets[node_key] > 0 else 0
                            
                            latest[node_key] = node_data
                            if node_key == "node1":
                                history_node1.appendleft(node_data)
                            elif node_key == "node3":
                                history_node3.appendleft(node_data)
        except Exception as e:
            print(f"❌ Waiting for Wokwi... ({e})")
            time.sleep(2)

threading.Thread(target=read_serial, daemon=True).start()

@app.route('/')
def index():
    return render_template('index.html')

@app.route('/api/data')
def get_data():
    return jsonify(latest)

@app.route('/api/history')
def get_history():
    return jsonify({
        "node1": list(history_node1),
        "node2": list(history_node2),
        "node3": list(history_node3)
    })

if __name__ == '__main__':
    app.run(host='0.0.0.0', port=5000)
