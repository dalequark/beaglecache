local http = require("http")

http.createServer(function (req, res)
  local body = "Hello world\n"

  res:on("error", function(err)
    msg = tostring(err)
    print("Error while sending a response: " .. msg)
  end)

  res:writeHead(200, {
    ["Content-Type"] = "text/plain",
    ["Content-Length"] = #body
  })
  res:finish(body)
end):listen(8080)

print("Server listening at http://localhost:8080/")