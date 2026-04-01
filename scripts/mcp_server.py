#!/usr/bin/env python3
"""
MCP Server for ESP32 Bridge - HTTP JSON-RPC Server

Compatible with esp_mcp HTTP client transport.

Usage:
    python scripts/mcp_server.py
"""

import json
from datetime import datetime
from http.server import HTTPServer, BaseHTTPRequestHandler

# MCP Protocol constants
MCP_VERSION = "2024-11-05"
SERVER_NAME = "esp32-bridge"
SERVER_VERSION = "1.0.0"

# Tool implementations
def get_device_status():
    return json.dumps({
        "status": "online",
        "uptime_seconds": 3600,
        "temperature": 36.5,
        "humidity": 65,
        "memory_free": 102400,
        "timestamp": datetime.now().isoformat(),
    })

def set_led(color: str, brightness: int):
    if brightness < 0 or brightness > 100:
        return json.dumps({"error": "Brightness must be between 0 and 100"})
    return json.dumps({
        "success": True,
        "color": color.lower(),
        "brightness": brightness,
        "applied_at": datetime.now().isoformat(),
    })

def read_sensor(sensor_id: str, sensor_type: str = "temperature"):
    readings = {
        "temperature": 23.5,
        "humidity": 55.0,
        "pressure": 1013.25,
    }
    value = readings.get(sensor_type.lower(), 0.0)
    return json.dumps({
        "sensor_id": sensor_id,
        "sensor_type": sensor_type,
        "value": value,
        "unit": "celsius" if sensor_type == "temperature" else ("%" if sensor_type == "humidity" else "hPa"),
        "timestamp": datetime.now().isoformat(),
    })

def control_motor(speed: int, direction: str = "forward"):
    if speed < 0 or speed > 100:
        return json.dumps({"error": "Speed must be between 0 and 100"})
    if direction not in ("forward", "reverse"):
        return json.dumps({"error": "Direction must be 'forward' or 'reverse'"})
    return json.dumps({
        "success": True,
        "speed": speed,
        "direction": direction,
        "applied_at": datetime.now().isoformat(),
    })

def get_time():
    return json.dumps({
        "datetime": datetime.now().isoformat(),
        "unix_timestamp": int(datetime.now().timestamp()),
        "timezone": "UTC",
    })

def echo(message: str):
    return json.dumps({"echo": message, "received_at": datetime.now().isoformat()})


# Tools registry: name -> (function, params_list)
TOOLS = {
    "get_device_status": (get_device_status, []),
    "set_led": (set_led, [{"name": "color", "type": "string"}, {"name": "brightness", "type": "integer"}]),
    "read_sensor": (read_sensor, [{"name": "sensor_id", "type": "string"}, {"name": "sensor_type", "type": "string"}]),
    "control_motor": (control_motor, [{"name": "speed", "type": "integer"}, {"name": "direction", "type": "string"}]),
    "get_time": (get_time, []),
    "echo": (echo, [{"name": "message", "type": "string"}]),
}


class MCPHandler(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def log_message(self, format, *args):
        print(f"[HTTP] {args[0]}")

    def send_json_response(self, status_code, data, content_type="application/json"):
        response = json.dumps(data).encode("utf-8")
        self.send_response(status_code)
        self.send_header("Content-Type", content_type)
        self.send_header("Content-Length", len(response))
        self.end_headers()
        self.wfile.write(response)

    def do_GET(self):
        """Handle GET requests - return server info"""
        parsed = self.path
        if parsed == "/mcp" or parsed == "/":
            # Server info response
            self.send_json_response(200, {
                "jsonrpc": "2.0",
                "result": {
                    "protocolVersion": MCP_VERSION,
                    "serverInfo": {
                        "name": SERVER_NAME,
                        "version": SERVER_VERSION,
                    },
                    "capabilities": {
                        "tools": {"listChanged": False},
                    },
                },
                "id": None
            })
        else:
            self.send_json_response(404, {
                "jsonrpc": "2.0",
                "error": {"code": -32600, "message": "Not found"},
                "id": None
            })

    def do_POST(self):
        """Handle POST requests - JSON-RPC calls"""
        parsed = self.path
        if parsed != "/mcp":
            self.send_json_response(404, {
                "jsonrpc": "2.0",
                "error": {"code": -32600, "message": "Not found"},
                "id": None
            })
            return

        # Read request body
        content_length = int(self.headers.get("Content-Length", 0))
        if content_length == 0:
            self.send_json_response(400, {
                "jsonrpc": "2.0",
                "error": {"code": -32700, "message": "Parse error"},
                "id": None
            })
            return

        body = self.rfile.read(content_length).decode("utf-8")
        print(f"[MCP] Received: {body}")

        # Parse JSON-RPC request
        try:
            request = json.loads(body)
        except json.JSONDecodeError:
            self.send_json_response(400, {
                "jsonrpc": "2.0",
                "error": {"code": -32700, "message": "Parse error"},
                "id": None
            })
            return

        method = request.get("method", "")
        req_id = request.get("id")
        params = request.get("params", {})

        # Handle MCP methods
        if method == "initialize":
            # Client initialization
            self.send_json_response(200, {
                "jsonrpc": "2.0",
                "result": {
                    "protocolVersion": MCP_VERSION,
                    "capabilities": {
                        "tools": {"listChanged": False},
                    },
                    "serverInfo": {
                        "name": SERVER_NAME,
                        "version": SERVER_VERSION,
                    },
                },
                "id": req_id
            })

        elif method == "tools/list":
            # List available tools
            tools_list = []
            for name, (func, params_schema) in TOOLS.items():
                tools_list.append({
                    "name": name,
                    "description": func.__doc__ or "",
                    "inputSchema": {
                        "type": "object",
                        "properties": {p["name"]: {"type": p["type"]} for p in params_schema},
                        "required": [p["name"] for p in params_schema] if params_schema else []
                    }
                })
            self.send_json_response(200, {
                "jsonrpc": "2.0",
                "result": {"tools": tools_list},
                "id": req_id
            })

        elif method == "tools/call":
            # Call a tool
            tool_name = params.get("name", "")
            arguments = params.get("arguments", {})

            if tool_name not in TOOLS:
                self.send_json_response(200, {
                    "jsonrpc": "2.0",
                    "error": {"code": -32602, "message": f"Unknown tool: {tool_name}"},
                    "id": req_id
                })
                return

            func, _ = TOOLS[tool_name]
            try:
                if arguments:
                    result = func(**arguments)
                else:
                    result = func()

                # Parse result if it's JSON string
                try:
                    result_obj = json.loads(result)
                except:
                    result_obj = result

                self.send_json_response(200, {
                    "jsonrpc": "2.0",
                    "result": {
                        "content": [{"type": "text", "text": result}]
                    },
                    "id": req_id
                })
            except Exception as e:
                self.send_json_response(200, {
                    "jsonrpc": "2.0",
                    "error": {"code": -32603, "message": str(e)},
                    "id": req_id
                })

        else:
            self.send_json_response(200, {
                "jsonrpc": "2.0",
                "error": {"code": -32601, "message": f"Method not found: {method}"},
                "id": req_id
            })


def main():
    server = HTTPServer(("0.0.0.0", 8000), MCPHandler)
    print(f"MCP Server started: http://0.0.0.0:8000/mcp")
    print(f"Tools available:")
    for name in TOOLS:
        print(f"  - {name}")
    print()
    print("Press Ctrl+C to stop")
    print()
    server.serve_forever()


if __name__ == "__main__":
    main()
