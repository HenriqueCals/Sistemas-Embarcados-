#!/usr/bin/env python3
"""Ponte Serial <-> MQTT da garrafa inteligente.

Roda no PC ligado ao Arduino por USB. Le as linhas que o smartwaterbottle.ino
imprime na Serial e as publica num broker MQTT, para o webapp mostrar os dados
de qualquer lugar (inclusive no Safari do iPhone). Tambem repassa ao Arduino os
comandos TARA e ZERAR enviados pelo webapp.

Topicos (<id> = identificador da garrafa):
  garrafa/<id>/serial  ponte -> app   linhas da Serial, separadas por \\n
  garrafa/<id>/status  ponte -> app   "online" / "offline" (retido)
  garrafa/<id>/cmd     app -> ponte   "TARA" ou "ZERAR"

Uso:
  python ponte.py                  (broker publico broker.emqx.io, sem cadastro)
  python ponte.py --host xxxx.s1.eu.hivemq.cloud --user garrafa --password ****
  python ponte.py --listar         (lista as portas seriais)
Sem --id, a ponte gera um ID aleatorio e o guarda em .garrafa-id. Ao iniciar,
ela mostra um link que ja configura o webapp no celular.
Os parametros tambem podem vir de variaveis de ambiente (veja --help).
"""
import argparse
import os
import queue
import secrets
import string
import sys
import threading
import time
from pathlib import Path
from urllib.parse import urlencode

try:
    import paho.mqtt.client as mqtt
    import serial
    from serial.tools import list_ports
except ImportError:
    sys.exit("Dependencias ausentes. Rode: pip install -r requirements.txt")

COMANDOS = {"TARA", "ZERAR"}   # unicos comandos repassados ao Arduino
INTERVALO_PUBLICACAO = 0.5     # s; agrupa as linhas para gastar menos mensagens
BROKER_PUBLICO = "broker.emqx.io"                 # gratuito, sem cadastro e sem senha
WSS_PUBLICO = "wss://broker.emqx.io:8084/mqtt"     # o mesmo broker, para o navegador
APP_URL = "https://henriquecals.github.io/Sistemas-Embarcados-/quinta/webapp/"
ARQUIVO_ID = Path(__file__).with_name(".garrafa-id")
PALAVRAS_ARDUINO = ("arduino", "ch340", "ch341", "cp210", "ftdi", "usb serial", "usb-serial")


def log(msg):
    print(time.strftime("%H:%M:%S"), msg, flush=True)


def parse_args():
    env = os.environ.get
    p = argparse.ArgumentParser(description="Ponte Serial <-> MQTT da garrafa inteligente.")
    p.add_argument("--serial", default=env("GARRAFA_SERIAL"),
                   help="porta do Arduino (ex.: COM3, /dev/ttyUSB0). Sem ela, tenta detectar. [GARRAFA_SERIAL]")
    p.add_argument("--baud", type=int, default=9600, help="deve ser igual ao Serial.begin do .ino (9600)")
    p.add_argument("--host", default=env("MQTT_HOST", BROKER_PUBLICO),
                   help=f"host do broker, sem wss:// (padrao: {BROKER_PUBLICO}) [MQTT_HOST]")
    p.add_argument("--port", type=int, default=int(env("MQTT_PORT", "8883")),
                   help="porta MQTT com TLS (8883 no EMQX e no HiveMQ Cloud) [MQTT_PORT]")
    p.add_argument("--user", default=env("MQTT_USER"), help="usuario do broker [MQTT_USER]")
    p.add_argument("--password", default=env("MQTT_PASS"), help="senha do broker [MQTT_PASS]")
    p.add_argument("--id", default=env("GARRAFA_ID"),
                   help="identificador da garrafa; deve ser o mesmo do webapp. Sem ele, usa/gera o de .garrafa-id [GARRAFA_ID]")
    p.add_argument("--wss", default=env("MQTT_WSS"),
                   help="endereco WebSocket do broker para o webapp (padrao: deduzido do host) [MQTT_WSS]")
    p.add_argument("--app-url", default=env("GARRAFA_APP_URL", APP_URL),
                   help="endereco do webapp, usado no link de configuracao [GARRAFA_APP_URL]")
    p.add_argument("--sem-tls", action="store_true", help="conecta sem TLS (so para brokers locais de teste)")
    p.add_argument("--listar", action="store_true", help="lista as portas seriais e sai")
    p.add_argument("-v", "--verbose", action="store_true", help="mostra cada linha recebida do Arduino")
    return p.parse_args()


def id_da_garrafa(a):
    """ID dificil de adivinhar: num broker publico, quem souber o ID ve os dados e envia comandos."""
    if a.id:
        return a.id
    try:
        salvo = ARQUIVO_ID.read_text(encoding="utf-8").strip()
        if salvo:
            return salvo
    except OSError:
        pass
    alfabeto = string.ascii_lowercase + string.digits
    novo = "garrafa-" + "".join(secrets.choice(alfabeto) for _ in range(10))
    try:
        ARQUIVO_ID.write_text(novo + "\n", encoding="utf-8")
    except OSError as e:
        log(f"Aviso: nao consegui salvar o ID em {ARQUIVO_ID} ({e}); ele mudara na proxima execucao.")
    return novo


def link_configuracao(a, gid):
    """Link que, aberto no celular, preenche a conexao remota do webapp (a senha nunca vai no link)."""
    wss = a.wss or (WSS_PUBLICO if a.host == BROKER_PUBLICO else f"wss://{a.host}:8884/mqtt")
    params = {"url": wss, "id": gid}
    if a.user:
        params["user"] = a.user
    return a.app_url + "#" + urlencode(params)


def detectar_porta():
    portas = list(list_ports.comports())
    provaveis = [p for p in portas
                 if any(w in f"{p.description} {p.manufacturer or ''}".lower() for w in PALAVRAS_ARDUINO)]
    if len(provaveis) == 1:
        return provaveis[0].device
    if len(portas) == 1:
        return portas[0].device
    return None


def main():
    a = parse_args()
    if a.listar:
        portas = list(list_ports.comports())
        if not portas:
            print("Nenhuma porta serial encontrada.")
        for p in portas:
            print(f"{p.device:12} {p.description}")
        return

    gid = id_da_garrafa(a)
    base = f"garrafa/{gid}"
    t_serial, t_status, t_cmd = f"{base}/serial", f"{base}/status", f"{base}/cmd"
    comandos = queue.Queue()
    estado = {"serial": False, "broker": False}

    client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2, client_id=f"ponte-{gid}-{os.getpid()}")
    if a.user:
        client.username_pw_set(a.user, a.password)
    if not a.sem_tls:
        client.tls_set()
    # Se a ponte cair sem avisar, o broker publica "offline" no lugar dela.
    client.will_set(t_status, "offline", qos=1, retain=True)
    client.reconnect_delay_set(1, 30)

    def publicar_status():
        client.publish(t_status, "online" if estado["serial"] else "offline", qos=1, retain=True)

    def on_connect(c, _u, _flags, rc, _props):
        if rc.is_failure:
            log(f"Broker recusou a conexao: {rc}. Confira host, usuario e senha.")
            return
        estado["broker"] = True
        log(f"Conectado ao broker {a.host}:{a.port} (topicos em {base}/)")
        c.subscribe(t_cmd, qos=1)
        publicar_status()

    def on_disconnect(_c, _u, _flags, rc, _props):
        log(f"Desconectado do broker ({rc}); tentando reconectar...")

    def on_message(_c, _u, msg):
        cmd = msg.payload.decode("utf-8", "replace").strip().upper()
        if cmd in COMANDOS:
            comandos.put(cmd)
        else:
            log(f"Comando ignorado: {cmd!r}")

    print()
    print(f"  Broker:          {a.host}:{a.port}")
    print(f"  ID da garrafa:   {gid}")
    print("  Abra no celular para configurar o webapp:")
    print(f"  {link_configuracao(a, gid)}")
    if a.user:
        print("  (a senha nao vai no link: digite-a no app)")
    print()

    client.on_connect = on_connect
    client.on_disconnect = on_disconnect
    client.on_message = on_message
    client.connect_async(a.host, a.port, keepalive=30)
    client.loop_start()

    def avisar_sem_broker():
        if not estado["broker"]:
            log(f"Ainda sem conexao com {a.host}:{a.port}. Verifique a internet; algumas redes "
                "(faculdade, empresa) bloqueiam a porta 8883. Continuo tentando...")
    aviso = threading.Timer(15, avisar_sem_broker)
    aviso.daemon = True
    aviso.start()

    try:
        while True:
            porta = a.serial or detectar_porta()
            if not porta:
                log("Arduino nao encontrado. Use --serial COMx (veja --listar). Tentando de novo em 3 s...")
                time.sleep(3)
                continue
            try:
                with serial.Serial(porta, a.baud, timeout=0.1) as ser:
                    log(f"Serial aberta em {porta} ({a.baud} baud)")
                    estado["serial"] = True
                    publicar_status()
                    while not comandos.empty():   # descarta comandos antigos
                        comandos.get_nowait()
                    buf, pendentes, ultimo = bytearray(), [], 0.0
                    while True:
                        while not comandos.empty():
                            cmd = comandos.get_nowait()
                            ser.write((cmd + "\n").encode())
                            log(f"Comando enviado ao Arduino: {cmd}")
                        buf += ser.read(ser.in_waiting or 1)
                        while (i := buf.find(b"\n")) >= 0:
                            texto = buf[:i].decode("utf-8", "replace").strip()
                            del buf[:i + 1]
                            if texto:
                                pendentes.append(texto)
                                if a.verbose:
                                    log(f"< {texto}")
                        if len(buf) > 2000:
                            buf.clear()
                        if pendentes and time.monotonic() - ultimo >= INTERVALO_PUBLICACAO:
                            client.publish(t_serial, "\n".join(pendentes) + "\n", qos=0)
                            pendentes.clear()
                            ultimo = time.monotonic()
            except serial.SerialException as e:
                if estado["serial"]:
                    estado["serial"] = False
                    publicar_status()
                log(f"Erro na Serial ({e}). Feche o Monitor Serial da IDE se estiver aberto. Tentando de novo em 3 s...")
                time.sleep(3)
    except KeyboardInterrupt:
        log("Encerrando...")
    finally:
        try:
            client.publish(t_status, "offline", qos=1, retain=True).wait_for_publish(2)
        except Exception:
            pass
        client.loop_stop()
        client.disconnect()


if __name__ == "__main__":
    main()
