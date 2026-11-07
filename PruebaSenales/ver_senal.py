# ver_senal.py - Bronquito
# Muestra EN VIVO lo que manda PruebaSenales.ino (ESP32), lo graba en un .csv y, al cerrar la
# ventana, grafica la medición completa (flujo, presión, volumen, tensiones y curva flujo-volumen).
#
# TECLAS (con la ventana del gráfico seleccionada):
#   1 = PIM   2 = PEM   3 = espiración   4 = inspiración   0 = nada
#       La marca queda puesta hasta que apretás otro número (columna "maniobra" del .csv).
#   t = repetir la tara en la placa (NO soplar)      f = filtro de 10 Hz sí / no
#   espacio = pausar / seguir el gráfico (la grabación sigue)
# Para terminar: cerrar la ventana (o Ctrl+C en la terminal).
#
# Requiere:  pip install pyserial pandas matplotlib
# IMPORTANTE: cerrar el Monitor Serie / Serial Plotter de Arduino antes de correr esto.
#
# Uso:
#   python ver_senal.py                 -> usa PUERTO (COM4)
#   python ver_senal.py COM5            -> con otro puerto
#   python ver_senal.py archivo.csv     -> grafica un .csv ya grabado, sin la placa

import sys
import time
from collections import deque

import pandas as pd
import matplotlib.pyplot as plt

PUERTO = "COM4"         # puerto del ESP32 (en Windows: Administrador de dispositivos > Puertos COM)
BAUD = 115200
VENTANA_S = 10          # segundos que se ven en vivo
REFRESCO_S = 0.05       # cada cuánto se redibuja
COLUMNAS = ["t_s", "v_flujo_V", "v_pim_V", "Q_Lmin", "P_cmH2O", "V_L", "fase",
            "v_flujo_crudo_V", "v_pim_crudo_V"]

MANIOBRAS = {0: "nada", 1: "PIM", 2: "PEM", 3: "Espiración", 4: "Inspiración"}
COLOR_MANIOBRA = {1: "#f4c7c3", 2: "#c7d9f4", 3: "#c9e8cf", 4: "#f6e2b3"}
AZUL, ROJO, GRIS = "#2a6fdb", "#d1495b", "#9a9a9a"


# =============================================================================
# CONEXIÓN
# =============================================================================
def buscar_puerto():
    from serial.tools import list_ports
    puertos = list(list_ports.comports())
    # Los ESP32 suelen aparecer como usbserial, SLAB_USBtoUART, wchusbserial o usbmodem
    for p in puertos:
        if any(s in p.device for s in ("usbserial", "SLAB", "wchusbserial", "usbmodem", "ttyUSB", "ttyACM", "COM")):
            return p.device
    print("No encontré el ESP32. Puertos disponibles:")
    for p in puertos:
        print("  ", p.device, "-", p.description)
    raise SystemExit("Pasá el puerto a mano: python ver_senal.py /dev/cu.XXXX")


def es_dato(partes):
    if len(partes) < 7:
        return False
    try:
        [float(p) for p in partes]
        return True
    except ValueError:
        return False


# =============================================================================
# GRABACIÓN CON GRÁFICO EN VIVO
# =============================================================================
def grabar(puerto):
    import serial
    nombre = time.strftime("medicion_%Y%m%d_%H%M%S.csv")
    ser = serial.Serial(puerto, BAUD, timeout=0)

    # Reinicia la placa para que haga la tara desde cero
    ser.dtr = False
    ser.rts = True
    time.sleep(0.1)
    ser.rts = False
    ser.reset_input_buffer()

    estado = {"maniobra": 0, "pausa": False, "seguir": True}
    cero = {"flujo": None, "pim": None}
    datos = {c: deque(maxlen=VENTANA_S * 100) for c in ("t", "q", "p", "vf", "vfc", "vp")}

    # ---- Ventana ----
    plt.ion()
    fig, ax = plt.subplots(3, 1, figsize=(12, 8), sharex=True)
    l_q, = ax[0].plot([], [], color=AZUL, lw=1.2)
    l_p, = ax[1].plot([], [], color=AZUL, lw=1.2)
    l_vfc, = ax[2].plot([], [], color=GRIS, lw=0.8, label="Flujo crudo")
    l_vf, = ax[2].plot([], [], color=AZUL, lw=1.4, label="Flujo filtrado")
    l_vp, = ax[2].plot([], [], color=ROJO, lw=1.0, label="Presión filtrada")
    ax[0].set_ylabel("Flujo [L/min]")
    ax[1].set_ylabel("Presión [cmH2O]")
    ax[2].set_ylabel("Tensión neta [mV]")
    ax[2].set_xlabel("Tiempo [s]")
    ax[2].legend(loc="upper left", frameon=False, fontsize=8)
    for a in ax:
        a.grid(True, color="#e6e6e6")
        a.axhline(0, color="#999", lw=0.8)
    titulo = fig.suptitle("Esperando datos...", fontsize=11)
    fig.text(0.01, 0.005, "1 PIM · 2 PEM · 3 espiración · 4 inspiración · 0 nada  |  "
                          "t tara · f filtro · espacio pausa  |  cerrar la ventana para terminar",
             fontsize=8, color="#555")

    def tecla(ev):
        if ev.key is None:
            return
        if ev.key in "01234":
            estado["maniobra"] = int(ev.key)
            print(f"  >> {MANIOBRAS[estado['maniobra']]}")
        elif ev.key == "t":
            ser.write(b"t")
        elif ev.key == "f":
            ser.write(b"f")
        elif ev.key == " ":
            estado["pausa"] = not estado["pausa"]

    def cerrar(_):
        estado["seguir"] = False

    fig.canvas.mpl_connect("key_press_event", tecla)
    fig.canvas.mpl_connect("close_event", cerrar)

    n = 0
    resto = ""
    ultimo_dibujo = 0.0
    print(f"Grabando en {nombre}  (cerrá la ventana para terminar)")
    with open(nombre, "w") as f:
        f.write(",".join(COLUMNAS + ["maniobra"]) + "\n")
        try:
            while estado["seguir"]:
                # ---- Leer todo lo que llegó ----
                bloque = ser.read(ser.in_waiting or 1).decode(errors="ignore")
                if bloque:
                    resto += bloque
                    *lineas, resto = resto.split("\n")
                    for linea in lineas:
                        linea = linea.strip()
                        if not linea:
                            continue
                        if linea.startswith("#"):
                            print(linea)
                            if "V_Zero Flujo" in linea:      # "# V_Zero Flujo: 1.6512 V   V_Zero PIM: 1.6498 V"
                                partes = linea.replace(":", " ").split()
                                cero["flujo"] = float(partes[partes.index("Flujo") + 1])
                                cero["pim"] = float(partes[partes.index("PIM") + 1])
                            continue
                        partes = linea.split(",")
                        if not es_dato(partes):
                            continue
                        partes = (partes + ["", ""])[:len(COLUMNAS)]
                        f.write(",".join(partes) + f",{estado['maniobra']}\n")
                        n += 1
                        t, vf, vp, q, p = (float(partes[i]) for i in (0, 1, 2, 3, 4))
                        vfc = float(partes[7]) if partes[7] else vf
                        cf = cero["flujo"] if cero["flujo"] is not None else vf
                        cp = cero["pim"] if cero["pim"] is not None else vp
                        datos["t"].append(t)
                        datos["q"].append(q)
                        datos["p"].append(p)
                        datos["vf"].append((vf - cf) * 1000)
                        datos["vfc"].append((vfc - cf) * 1000)
                        datos["vp"].append((vp - cp) * 1000)

                # ---- Redibujar ----
                ahora = time.time()
                if ahora - ultimo_dibujo >= REFRESCO_S:
                    ultimo_dibujo = ahora
                    if datos["t"] and not estado["pausa"]:
                        t = list(datos["t"])
                        l_q.set_data(t, datos["q"])
                        l_p.set_data(t, datos["p"])
                        l_vfc.set_data(t, datos["vfc"])
                        l_vf.set_data(t, datos["vf"])
                        l_vp.set_data(t, datos["vp"])
                        for a in ax:
                            a.relim()
                            a.autoscale_view(scalex=False)
                        ax[0].set_xlim(max(0, t[-1] - VENTANA_S), max(VENTANA_S, t[-1]))
                        # Ruido del último segundo (desvío estándar del flujo filtrado, en mV)
                        ult = list(datos["vf"])[-100:]
                        media = sum(ult) / len(ult)
                        ruido = (sum((x - media) ** 2 for x in ult) / len(ult)) ** 0.5
                        titulo.set_text(f"{nombre}  |  {n} muestras  |  marca: {MANIOBRAS[estado['maniobra']]}  |  "
                                        f"ruido del flujo (último 1 s): {ruido:.2f} mV")
                    fig.canvas.draw_idle()
                    fig.canvas.flush_events()
                else:
                    time.sleep(0.002)
        except KeyboardInterrupt:
            pass
    ser.close()
    plt.ioff()
    plt.close(fig)

    if n == 0:
        print("No llegó ningún dato. ¿Se subió PruebaSenales.ino a la placa? ¿Está cerrado el monitor serie?")
        raise SystemExit
    print(f"Guardadas {n} muestras en {nombre}")
    return nombre


# =============================================================================
# GRÁFICO DE LA MEDICIÓN COMPLETA
# =============================================================================
def marcar_maniobras(df, ejes, eje_texto):
    if "maniobra" not in df:
        return
    tramo = (df.maniobra != df.maniobra.shift()).cumsum()
    for _, seg in df.groupby(tramo):
        m = int(seg.maniobra.iloc[0])
        if m == 0:
            continue
        t_ini, t_fin = seg.t_s.iloc[0], seg.t_s.iloc[-1]
        for a in ejes:
            a.axvspan(t_ini, t_fin, color=COLOR_MANIOBRA[m], alpha=0.6, lw=0, zorder=0)
        eje_texto.text((t_ini + t_fin) / 2, 1.02, MANIOBRAS[m], ha="center", va="bottom",
                       fontsize=9, transform=eje_texto.get_xaxis_transform())


def graficar(nombre):
    df = pd.read_csv(nombre)
    q_Ls = df.Q_Lmin / 60.0
    # Cero de cada canal: la mediana del primer segundo (la placa hace la tara en reposo)
    reposo = df[df.t_s < df.t_s.iloc[0] + 1.0]
    cf, cp = reposo.v_flujo_V.median(), reposo.v_pim_V.median()

    fig = plt.figure(figsize=(15, 9))
    gs = fig.add_gridspec(4, 2, width_ratios=[1.7, 1])
    ax0 = fig.add_subplot(gs[0, 0])
    ax1 = fig.add_subplot(gs[1, 0], sharex=ax0)
    ax2 = fig.add_subplot(gs[2, 0], sharex=ax0)
    ax3 = fig.add_subplot(gs[3, 0], sharex=ax0)

    ax0.plot(df.t_s, df.Q_Lmin, color=AZUL, lw=1);  ax0.set_ylabel("Flujo [L/min]")
    ax1.plot(df.t_s, df.P_cmH2O, color=AZUL, lw=1); ax1.set_ylabel("Presión [cmH2O]")
    ax2.plot(df.t_s, df.V_L, color=AZUL, lw=1);     ax2.set_ylabel("Volumen [L]")
    if "v_flujo_crudo_V" in df and df.v_flujo_crudo_V.notna().any():
        ax3.plot(df.t_s, (df.v_flujo_crudo_V - cf) * 1000, color=GRIS, lw=0.6, label="Flujo crudo")
    ax3.plot(df.t_s, (df.v_flujo_V - cf) * 1000, color=AZUL, lw=1.1, label="Flujo filtrado")
    ax3.plot(df.t_s, (df.v_pim_V - cp) * 1000, color=ROJO, lw=0.9, label="Presión filtrada")
    ax3.set_ylabel("Tensión neta [mV]")
    ax3.set_xlabel("Tiempo [s]")
    ax3.legend(loc="upper left", frameon=False, fontsize=8)
    marcar_maniobras(df, (ax0, ax1, ax2, ax3), ax0)

    # Curva flujo-volumen: solo las muestras con flujo
    ax4 = fig.add_subplot(gs[:, 1])
    esp, ins = df.fase == 1, df.fase == -1
    ax4.plot(df.V_L[esp], q_Ls[esp], ".", ms=3, color=AZUL, label="Espiración")
    ax4.plot(df.V_L[ins], q_Ls[ins], ".", ms=3, color=ROJO, label="Inspiración")
    ax4.set_xlabel("Volumen [L]")
    ax4.set_ylabel("Flujo [L/s]")
    ax4.set_title("Curva flujo-volumen")
    ax4.legend(frameon=False)

    for a in (ax0, ax1, ax2, ax3, ax4):
        a.grid(True, color="#e6e6e6")
        a.axhline(0, color="#999", lw=0.8)

    # Ruido en reposo (primer segundo), para comparar entre mediciones
    rf = (reposo.v_flujo_V * 1000).std()
    rp = (reposo.P_cmH2O).std()
    fig.suptitle(f"{nombre}  |  ruido en reposo (primer segundo): flujo {rf:.2f} mV, presión {rp:.2f} cmH2O",
                 fontsize=11)
    print(f"Ruido en reposo (primer segundo): flujo {rf:.2f} mV | presión {rp:.2f} cmH2O")
    plt.tight_layout()
    plt.savefig(nombre.replace(".csv", ".png"), dpi=130)
    plt.show()


if __name__ == "__main__":
    arg = sys.argv[1] if len(sys.argv) > 1 else None
    if arg and arg.lower().endswith(".csv"):
        graficar(arg)
    else:
        graficar(grabar(arg or PUERTO))
