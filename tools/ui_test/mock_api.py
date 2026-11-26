"""Mock af IHC Bridge-API'et til test af web-UI'et. Se README.md."""
import json, sys, base64, hashlib, threading, time, struct, socket
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
UI = sys.argv[1]
OPSAT = sys.argv[2] == "1"
UDN = {(1,1):"Stue loftlys",(1,2):"Spisebord",(1,3):"Køkken spots",(1,4):"Gang loft",(1,5):"Soveværelse",(1,6):"Badeværelse",
       (1,7):"Udendørs spots",(1,8):"Terrasse lys",(2,1):"Carport",(2,2):"Havebelysning",(2,3):"Cirkulationspumpe",(2,4):"Ventilation bad",
       (2,5):"Varmestyring",(2,6):"Markise",(2,7):"Stikkontakt kontor",(2,8):"Garageport relæ"}
UDT = {(2,3):"kontakt",(2,4):"ventilator",(2,5):"kontakt",(2,6):"kontakt",(2,7):"stikkontakt",(2,8):"kontakt"}
INN = {(1,1):"Stue lyskontakt",(1,2):"Entré PIR",(1,3):"Køkken dørkontakt",(1,4):"Soveværelse tryk",(1,5):"Gang bevægelse",
       (1,6):"Badeværelse",(1,7):"Stue",(1,8):"Garageport sensor"}
INT = {(1,2):"bevaegelse",(1,3):"doer",(1,5):"bevaegelse",(1,6):"temperatur",(1,7):"temperatur",(1,8):"doer"}
TEMP = {(1,6):{"online":True,"temperatur":23.4,"gulv":26.1,"alder_s":3},(1,7):{"online":True,"temperatur":21.5,"fugt":45.0,"alder_s":2}}
st = {"opsat":OPSAT,"kode":"hemmelig123","ud":[0b10101011,0b01010001]+[0]*14,"aktiv_ud":{1,2},"aktiv_ind":{1,2},
      "ind":[0x0013,0]+[0]*6,"online":[True,False]+[False]*6,"regler":[{"type":"kip","ind_modul":1,"ind_kanal":1,"ud_modul":1,"ud_kanal":1},
      {"type":"kip","ind_modul":1,"ind_kanal":4,"ud_modul":1,"ud_kanal":5}],"ws":[]}
def io():
    return {"udgange":[{"modul":m,"aktiv":m in st["aktiv_ud"],"tilstand":st["ud"][m-1],"kanaler":[{"navn":UDN.get((m,k),f"Udgang {m}.{k}"),
              "eget_navn":(m,k) in UDN,"type":UDT.get((m,k),"lys")} for k in range(1,9)]} for m in range(1,17)],
            "indgange":[{"modul":m,"aktiv":m in st["aktiv_ind"],"online":st["online"][m-1],"tilstand":st["ind"][m-1],"kanaler":[dict({"navn":INN.get((m,k),f"Indgang {m}.{k}"),
              "eget_navn":(m,k) in INN,"type":INT.get((m,k),"tryk")}, **({"temperatur":TEMP.get((m,k))} if INT.get((m,k))=="temperatur" else {})) for k in range(1,17)]} for m in range(1,9)]}
def status():
    return {"system":{"version":"2.0.0-dev","oppetid_s":1234567,"fri_ram":186000,"fri_psram":8100000,"enheds_id":"a1b2c3","firmware_afventer_bekraeftelse":False},
            "wifi":{"forbundet":True,"ssid":"Hjemme","ip":"192.168.1.50","rssi":-56,"vaertsnavn":"ihc-bridge","ap":"slukket","forsoeg":0,"naeste_forsoeg_s":0},
            "mqtt":{"aktiveret":True,"forbundet":True,"fejl":"","sendt":4211,"modtaget":87},
            "bus":{"frames_sendt":6512345,"max_isr_us":6,"inputmoduler":[{"modul":m,"online":st["online"][m-1],"frames_ok":64000 if m==1 else 0,
              "paritet16":64000 if m==1 else 0,"paritet8":61000 if m==1 else 0,"paritetsfejl":0,"pulsfejl":2 if m==1 else 0} for m in range(1,9)],
            "temperaturfoelere":[{"modul":1,"kanal":6,"online":True,"gyldige":712,"fejl":3,"alder_s":3},{"modul":1,"kanal":7,"online":True,"gyldige":715,"fejl":1,"alder_s":2}]}}
# Online-opdatering: uden testversioner er der intet nyt; med testversioner
# findes 2.0.0-beta.2. Søgning tager 1 s, installation ca. 5 s.
OPD = {"tilstand":"klar","test":False,"t":0.0,"fejl":""}
BETA = {"version":"2.0.0-beta.2","navn":"IHC Bridge 2.0.0-beta.2","dato":"2026-11-27","side":"https://github.com/K3nnis/IHC-Output-Bridge/releases/tag/v2.0.0-beta.2",
        "noter":"Nyt:\n- Online-opdatering fra GitHub\n- <b>Ingen HTML</b> i noterne vises som tekst","stoerrelse":1263778,"test":True}
def opdatering():
    d = time.time() - OPD["t"]
    if OPD["tilstand"]=="soeger" and d>1: OPD["tilstand"] = "fundet" if OPD["test"] else "nyeste"
    pct = 0
    if OPD["tilstand"]=="henter":
        pct = min(100, int(d*20))
        if pct>=100: OPD["tilstand"]="genstarter"
    if OPD["tilstand"]=="genstarter": pct = 100
    j = {"tilstand":OPD["tilstand"],"installeret":"2.0.0-dev","fejl":OPD["fejl"],"procent":pct,"testversioner":OPD["test"],"sidst_soegt_s":-1}
    if OPD["test"] and OPD["tilstand"] in ("fundet","henter","genstarter"): j["release"]=BETA
    return j
def ws_frame(txt):
    b=txt.encode(); h=bytes([0x81]); n=len(b)
    h+= bytes([n]) if n<126 else bytes([126])+struct.pack(">H",n)
    return h+b
def ws_push(obj):
    for s in list(st["ws"]):
        try: s.sendall(ws_frame(json.dumps(obj)))
        except Exception: st["ws"].remove(s)
def ws_reader(sock):
    try:
        while True:
            h=sock.recv(2)
            if len(h)<2: break
            n=h[1]&127
            if n==126: n=struct.unpack(">H",sock.recv(2))[0]
            mask=sock.recv(4); data=bytearray(sock.recv(n))
            for i in range(n): data[i]^=mask[i%4]
            if h[0]&15==8: break
            d=json.loads(data)
            if d.get("type")=="udgang":
                st["ud"][d["modul"]-1]^=1<<(d["kanal"]-1); ws_push({"type":"tilstand","udgange":{str(d["modul"]):st["ud"][d["modul"]-1]}})
    except Exception: pass
class H(BaseHTTPRequestHandler):
    protocol_version="HTTP/1.1"
    def log_message(self,*a): pass
    def send(self,code,obj,cookie=None):
        b=json.dumps(obj).encode(); self.send_response(code); self.send_header("Content-Type","application/json")
        if cookie: self.send_header("Set-Cookie",cookie)
        self.send_header("Content-Length",str(len(b))); self.end_headers(); self.wfile.write(b)
    def li(self): return "ihc_session=ok" in (self.headers.get("Cookie") or "")
    def body(self):
        n=int(self.headers.get("Content-Length",0)); return json.loads(self.rfile.read(n)) if n else {}
    def do_GET(self):
        p=self.path
        if p=="/":
            b=open(UI,"rb").read(); self.send_response(200); self.send_header("Content-Type","text/html; charset=utf-8")
            self.send_header("Content-Length",str(len(b))); self.end_headers(); self.wfile.write(b); return
        if p=="/ws":
            if not self.li(): return self.send(401,{})
            key=self.headers["Sec-WebSocket-Key"]; acc=base64.b64encode(hashlib.sha1((key+"258EAFA5-E914-47DA-95CA-C5AB0DC85B11").encode()).digest()).decode()
            self.send_response(101); self.send_header("Upgrade","websocket"); self.send_header("Connection","Upgrade"); self.send_header("Sec-WebSocket-Accept",acc); self.end_headers()
            self.wfile.flush(); sock=self.connection; st["ws"].append(sock)
            sock.sendall(ws_frame(json.dumps({"type":"tilstand","udgange":{str(m+1):st["ud"][m] for m in range(16)},"indgange":{str(m+1):{"online":st["online"][m],"tilstand":st["ind"][m]} for m in range(8)}})))
            ws_reader(sock); self.close_connection=True; return
        if p=="/api/opsaetning":
            if OPD["tilstand"]=="genstarter": OPD.update(tilstand="klar")  # "genstartet"
            return self.send(200,{"opsat":st["opsat"],"version":"2.0.0-dev","wifi":{"forbundet":st["opsat"],"ssid":"Hjemme" if st["opsat"] else "","ip":"192.168.1.50" if st["opsat"] else "","forsoeg":1}})
        if p=="/api/wifi/scan": return self.send(200,{"netvaerk":[{"ssid":"Hjemme","rssi":-50,"krypteret":True},{"ssid":"Nabo 5G","rssi":-82,"krypteret":True}]})
        if not self.li(): return self.send(401,{"ok":False,"fejl":"Log ind først"})
        if p=="/api/io": return self.send(200,io())
        if p=="/api/status": return self.send(200,status())
        if p=="/api/regler": return self.send(200,{"regler":st["regler"]})
        if p=="/api/wifi": return self.send(200,{"ssid":"Hjemme","vaertsnavn":"ihc-bridge","fast_ip":False,"ip":"","maske":"","gateway":"","dns":""})
        if p=="/api/mqtt": return self.send(200,{"vaert":"192.168.1.10","port":1883,"bruger":"ihc","har_kode":True,"klient_id":"","basis_topic":"ihc/ihc-bridge","payload_on":"ON","payload_off":"OFF","retain":True,"qos":0,"keepalive":30,"discovery":True,"discovery_prefix":"homeassistant"})
        if p.startswith("/api/log"):
            efter=int(p.split("=")[1]) if "=" in p else 0
            l=[{"nr":i,"tekst":t} for i,t in enumerate(["I (312) ihc-bridge: Chip: ESP32-S3 rev 2, 2 kerner","I (320) ihc_bus: Bus startet på kerne 1: tick 156 µs, sample 39 µs",
              "I (1120) wifi: Forbundet med IP 192.168.1.50","W (5120) mqtt: Forbindelsen til brokeren er tabt","I (7120) mqtt: Forbundet til broker 192.168.1.10:1883","I (9000) tilstand: Input 1.1: ON","I (9020) tilstand: Udgang 1.1: ON"],1) if i>efter]
            return self.send(200,{"linjer":l,"sidste":7})
        if p=="/api/opdatering": return self.send(200,opdatering())
        if p=="/api/eksport": return self.send(200,{"format":"ihc-bridge-konfig","version":1})
        self.send(404,{"ok":False,"fejl":"Ikke fundet"})
    def do_POST(self):
        p=self.path
        if p=="/api/firmware":
            n=int(self.headers.get("Content-Length",0)); self.rfile.read(n); return self.send(400,{"ok":False,"fejl":"Filen er ikke firmware til IHC Bridge"})
        b=self.body()
        if p=="/api/opsaetning":
            st["opsat"]=True; return self.send(200,{"ok":True},"ihc_session=ok; Path=/")
        if p=="/api/login":
            if b.get("kode")==st["kode"]: return self.send(200,{"ok":True},"ihc_session=ok; Path=/")
            return self.send(401,{"ok":False,"fejl":"Forkert adgangskode"})
        if p=="/api/logud": return self.send(200,{"ok":True},"ihc_session=; Path=/; Max-Age=0")
        if not self.li(): return self.send(401,{"ok":False,"fejl":"Log ind først"})
        if p=="/api/udgang":
            st["ud"][b["modul"]-1]^=1<<(b["kanal"]-1); ws_push({"type":"tilstand","udgange":{str(b["modul"]):st["ud"][b["modul"]-1]}}); return self.send(200,{"ok":True})
        if p=="/api/regler": st["regler"]=b["regler"]; return self.send(200,{"ok":True})
        if p=="/api/moduler": st["aktiv_ud"]=set(b["udgange"]); st["aktiv_ind"]=set(b["indgange"]); return self.send(200,{"ok":True})
        if p=="/api/opdatering/soeg": OPD.update(tilstand="soeger",t=time.time(),fejl=""); return self.send(200,{"ok":True})
        if p=="/api/opdatering/indstillinger": OPD.update(test=b["testversioner"],tilstand="klar"); return self.send(200,{"ok":True})
        if p=="/api/opdatering/installer":
            if OPD["tilstand"]!="fundet" or b.get("version")!=BETA["version"]: return self.send(400,{"ok":False,"fejl":"Søg efter opdatering først"})
            OPD.update(tilstand="henter",t=time.time()); return self.send(200,{"ok":True})
        if p=="/api/kanal":
            k=(b["modul"],b["kanal"])
            if b["retning"]=="udgang":
                if b["navn"]: UDN[k]=b["navn"]
                else: UDN.pop(k,None)
                UDT[k]=b["type"]
            return self.send(200,{"ok":True})
        if p in ("/api/mqtt","/api/wifi","/api/kode","/api/import"):
            if p=="/api/kode" and b.get("gammel")!=st["kode"]: return self.send(400,{"ok":False,"fejl":"Den nuværende adgangskode er forkert"})
            return self.send(200,{"ok":True})
        self.send(404,{"ok":False})
def tryk():
    n = 0
    while True:
        time.sleep(1.5); st["ind"][0]^=0x0100; ws_push({"type":"tilstand","indgange":{"1":{"online":True,"tilstand":st["ind"][0]}}})
        n += 1
        if n % 2 == 0:
            TEMP[(1,7)]["temperatur"] = round(TEMP[(1,7)]["temperatur"] + 0.1, 1)
            ws_push({"type":"tilstand","temperaturer":{"1.6":TEMP[(1,6)],"1.7":TEMP[(1,7)]}})
threading.Thread(target=tryk,daemon=True).start()
ThreadingHTTPServer(("127.0.0.1",8766),H).serve_forever()
