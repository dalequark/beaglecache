local ffi = require("ffi")

ffi.cdef[[
	char* sayHello(char* myName);
]]
simple = ffi.load("./simpleLib.so");
param = ffi.new("char[5]")
ffi.copy(param, "Dale\0")
print(ffi.string(simple.sayHello(param)))