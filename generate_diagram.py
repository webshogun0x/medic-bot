import xml.etree.ElementTree as ET
import uuid

def create_mx_cell(parent, id, value, style, vertex, x, y, width, height):
    cell = ET.SubElement(parent, "mxCell")
    cell.set("id", str(id))
    cell.set("value", value)
    cell.set("style", style)
    cell.set("vertex", str(vertex))
    cell.set("parent", "1")
    geo = ET.SubElement(cell, "mxGeometry")
    geo.set("x", str(x))
    geo.set("y", str(y))
    geo.set("width", str(width))
    geo.set("height", str(height))
    geo.set("as", "geometry")
    return cell

def create_mx_edge(parent, id, value, style, edge, source, target):
    cell = ET.SubElement(parent, "mxCell")
    cell.set("id", str(id))
    cell.set("value", value)
    cell.set("style", style)
    cell.set("edge", str(edge))
    cell.set("source", str(source))
    cell.set("target", str(target))
    cell.set("parent", "1")
    geo = ET.SubElement(cell, "mxGeometry")
    geo.set("relative", "1")
    geo.set("as", "geometry")
    return cell

root_mx = ET.Element("mxfile")
root_mx.set("host", "Electron")
root_mx.set("agent", "GeminiCLI")

diagram = ET.SubElement(root_mx, "diagram")
diagram.set("id", str(uuid.uuid4()))
diagram.set("name", "MediBot Advanced System Architecture")

mx_graph_model = ET.SubElement(diagram, "mxGraphModel")
mx_graph_model.set("dx", "1422")
mx_graph_model.set("dy", "798")
mx_graph_model.set("grid", "1")
mx_graph_model.set("gridSize", "10")
mx_graph_model.set("guides", "1")
mx_graph_model.set("tooltips", "1")
mx_graph_model.set("connect", "1")
mx_graph_model.set("arrows", "1")
mx_graph_model.set("fold", "1")
mx_graph_model.set("page", "1")
mx_graph_model.set("pageScale", "1")
mx_graph_model.set("pageWidth", "827")
mx_graph_model.set("pageHeight", "1169")
mx_graph_model.set("math", "0")
mx_graph_model.set("shadow", "0")

root = ET.SubElement(mx_graph_model, "root")

ET.SubElement(root, "mxCell", id="0")
ET.SubElement(root, "mxCell", id="1", parent="0")

# Styles
style_esp = "rounded=1;whiteSpace=wrap;html=1;fillColor=#dae8fc;strokeColor=#6c8ebf;fontStyle=1"
style_cloud = "ellipse;shape=cloud;whiteSpace=wrap;html=1;fillColor=#f5f5f5;strokeColor=#666666;fontColor=#333333;fontStyle=1"
style_web = "rounded=1;whiteSpace=wrap;html=1;fillColor=#d5e8d4;strokeColor=#82b366;fontStyle=1"
style_sensor = "rounded=1;whiteSpace=wrap;html=1;fillColor=#fff2cc;strokeColor=#d6b656;"
style_storage = "shape=cylinder3;whiteSpace=wrap;html=1;boundedLbl=1;backgroundOutline=1;size=15;fillColor=#e1d5e7;strokeColor=#9673a6;"
style_ai = "ellipse;shape=cloud;whiteSpace=wrap;html=1;fillColor=#ffe6cc;strokeColor=#d79b00;fontStyle=1"
style_module = "rounded=1;whiteSpace=wrap;html=1;fillColor=#f8cecc;strokeColor=#b85450;"

# Components
# Core
ctrl = create_mx_cell(root, "ctrl", "Main Controller\n(MEDIC_BOT_CONTROL_MAIN)", style_esp, 1, 320, 300, 200, 80)
disp = create_mx_cell(root, "disp", "Sunton Display\n(LVGL UI)", style_esp, 1, 40, 300, 180, 80)
hw_mod = create_mx_cell(root, "hw_mod", "Height & Weight Module", style_esp, 1, 320, 540, 200, 80)

# Cloud
firebase = create_mx_cell(root, "firebase", "Firebase Cloud\n(RTDB)", style_cloud, 1, 330, 40, 180, 100)
grok_ai = create_mx_cell(root, "grok_ai", "Grok Cloud AI\n(Medical Analysis)", style_ai, 1, 600, 40, 180, 100)

# Web
backend = create_mx_cell(root, "backend", "Web Backend\n(Express)", style_web, 1, 610, 180, 160, 60)
frontend = create_mx_cell(root, "frontend", "Web Dashboard\n(React)", style_web, 1, 610, 280, 160, 60)

# Local Extras
sd_card = create_mx_cell(root, "sd_card", "SD Card\n(Local SQLite/Logs)", style_storage, 1, 370, 420, 100, 80)
mp3_mod = create_mx_cell(root, "mp3_mod", "MP3 Player\n(Voice Feedback)", style_module, 1, 150, 420, 140, 60)
local_sensors = create_mx_cell(root, "local_sensors", "RFID, Fingerprint,\nOximeter", style_sensor, 1, 580, 420, 160, 60)
hw_sensors = create_mx_cell(root, "hw_sensors", "Load Cells, ToF Sensor", style_sensor, 1, 340, 660, 160, 60)

# Connections
create_mx_edge(root, "e1", "UART", "endArrow=classic;startArrow=classic;html=1;rounded=0;", 1, "ctrl", "disp")
create_mx_edge(root, "e2", "ESP-NOW", "endArrow=classic;startArrow=classic;html=1;rounded=0;", 1, "ctrl", "hw_mod")
create_mx_edge(root, "e3", "Wi-Fi", "endArrow=classic;html=1;rounded=0;", 1, "ctrl", "firebase")
create_mx_edge(root, "e4", "SPI/I2C", "endArrow=classic;html=1;rounded=0;", 1, "ctrl", "sd_card")
create_mx_edge(root, "e5", "Digital Signal", "endArrow=classic;html=1;rounded=0;", 1, "ctrl", "mp3_mod")
create_mx_edge(root, "e6", "I2C/GPIO", "endArrow=classic;html=1;rounded=0;", 1, "ctrl", "local_sensors")
create_mx_edge(root, "e7", "I2C/GPIO", "endArrow=classic;html=1;rounded=0;", 1, "hw_mod", "hw_sensors")
create_mx_edge(root, "e8", "Firebase SDK", "endArrow=classic;startArrow=classic;html=1;rounded=0;", 1, "backend", "firebase")
create_mx_edge(root, "e9", "Grok API", "endArrow=classic;startArrow=classic;html=1;rounded=0;", 1, "backend", "grok_ai")
create_mx_edge(root, "e10", "REST", "endArrow=classic;startArrow=classic;html=1;rounded=0;", 1, "frontend", "backend")

# Save to file
tree = ET.ElementTree(root_mx)
with open("system_architecture.drawio", "wb") as f:
    tree.write(f, encoding="UTF-8", xml_declaration=True)
