-- HTTP Example Lua Script
-- Demonstrates HTTP requests using esp_http_client

print("=== HTTP GET Example ===")
local response, status = http_get("https://example.com")
print("Status:", status)
print("Response:", response)

print("\n=== HTTP POST Example ===")
local post_data = '{"name":"test","value":123}'
local response, status = http_post("https://httpbin.org/post", post_data, "application/json")
print("Status:", status)
print("Response:", response)

print("\n=== HTTP PUT Example ===")
local put_data = '{"name":"updated"}'
local response, status = http_put("https://httpbin.org/put", put_data, "application/json")
print("Status:", status)
print("Response:", response)

print("\n=== HTTP DELETE Example ===")
local response, status = http_delete("https://httpbin.org/delete")
print("Status:", status)
print("Response:", response)

print("\n=== All HTTP tests completed ===")
