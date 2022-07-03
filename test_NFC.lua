require('lib.strict')
require('lib.apdu')
if card.connect() then
    root = nodes.root()
    mycard = root:append({classname="card", label = "Prueba comando"})
    
    command = bytes.new(8,"00 A4 04 00 0E 31 50 41 59 2E 53 59 53 2E 44 44 46 30 31 00")
    sw,resp = card.send(command)
    mycard:append({classname = "block", label = "respuesta a", size=#resp,val=resp})
    
    command = bytes.new(8,"00 B2 01 0C 00")
    sw,resp = card.send(command)
    mycard:append({classname = "block", label = "respuesta b", size=#resp,val=resp})
    
    command = bytes.new(8,"00 B2 02 0C 00")
    sw,resp = card.send(command)
    mycard:append({classname = "block", label = "respuesta c", size=#resp,val=resp})
    
    command = bytes.new(8,"00 A4 04 00 07 A0 00 00 00 03 10 10 00")
    sw,resp = card.send(command)
    mycard:append({classname = "block", label = "respuesta d", size=#resp,val=resp})
    
    card.disconnect()
end
