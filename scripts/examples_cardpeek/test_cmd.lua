require('lib.strict')
require('lib.apdu')
if card.connect() then
    root = nodes.root()
    mycard = root:append({classname="card", label = "Prueba comando"})
    command = bytes.new(8,"00 A4 04 00 0E 31 50 41 59 2E 53 59 53 2E 44 44 46 30 31 00")
    sw,resp = card.send(command)
    mycard:append({classname = "block", label = "respuesta", size=#resp,val=resp})
    card.disconnect()
end
