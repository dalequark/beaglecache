-- load config file
local config = require("luaconf")


if isSWaprox == true then
  print("Hello, I'm S Waprox!")
else
  print("Hello, I'm R Waprox!")
end

if isSWaprox then
  local sWaprox = require("sWaprox")
  sWaproxMain()
else
  local rWaprox = require("rWaprox")
  rWaproxMain()
end
