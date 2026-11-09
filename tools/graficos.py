"""
graficos.py
Atividade 3 - Gera os gráficos (resultados/graficos) e as tabelas LaTeX (relatorio/gerado)
a partir dos CSV produzidos pelo protocolo automático (resultados/dados).

Uso (na raiz do projeto):  python tools/graficos.py
Requer: matplotlib.
"""
import csv
import os
import sys

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib.ticker import FuncFormatter, LogLocator, NullFormatter

RAIZ = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DADOS = os.path.join(RAIZ, "resultados", "dados")
SAIDA_G = os.path.join(RAIZ, "resultados", "graficos")
SAIDA_T = os.path.join(RAIZ, "relatorio", "gerado")
LIMITE = 60.0

CONFIGS = "ABCDEF"
# Paleta categórica validada (ordem fixa) + codificação secundária: marcador e traço
COR = {"A": "#2a78d6", "B": "#eb6834", "C": "#1baf7a", "D": "#eda100", "E": "#e87ba4", "F": "#6250d6"}
MARCADOR = {"A": "o", "B": "s", "C": "^", "D": "o", "E": "s", "F": "^"}
TRACO = {"A": "-", "B": "-", "C": "-", "D": "--", "E": "--", "F": "--"}
NOME = {
    "A": "A: sem luz, sem textura", "B": "B: omni, sem textura", "C": "C: spot, sem textura",
    "D": "D: sem luz, com textura", "E": "E: omni, com textura", "F": "F: spot, com textura",
}
TINTA = "#0b0b0b"
TINTA2 = "#52514e"
GRADE = "#d9d8d4"
REF = "#e34948"  # linha de referência de 60 FPS

plt.rcParams.update({
    "font.family": "DejaVu Sans", "font.size": 9, "axes.titlesize": 10, "axes.labelsize": 9,
    "axes.edgecolor": TINTA2, "axes.labelcolor": TINTA, "xtick.color": TINTA2, "ytick.color": TINTA2,
    "axes.spines.top": False, "axes.spines.right": False, "legend.frameon": False,
    "legend.fontsize": 8, "lines.linewidth": 1.6, "lines.markersize": 4.5, "savefig.dpi": 200,
})


# ---------------------------------------------------------------------------
# Leitura
# ---------------------------------------------------------------------------
def ler(rotulo, sufixo="pontos"):
    caminho = os.path.join(DADOS, f"{rotulo}_{sufixo}.csv")
    if not os.path.exists(caminho):
        return []
    with open(caminho, encoding="utf-8") as f:
        linhas = list(csv.DictReader(f))
    for r in linhas:
        for k, v in r.items():
            try:
                r[k] = float(v) if any(c in v for c in ".e") else int(v)
            except (ValueError, TypeError):
                pass
    return linhas


def ler_combinado(rotulo):
    """Lê <rotulo>_pontos.csv e aplica arquivos de reposição <rotulo>_<CONFIGS>_pontos.csv:
    as configurações remedidas num arquivo de reposição substituem as do arquivo principal
    (usado quando uma execução foi interrompida e uma configuração precisou ser medida de novo)."""
    pontos = ler(rotulo)
    if not os.path.isdir(DADOS):
        return pontos
    for nome in sorted(os.listdir(DADOS)):
        prefixo = rotulo + "_"
        if not (nome.startswith(prefixo) and nome.endswith("_pontos.csv")):
            continue
        cfgs = nome[len(prefixo):-len("_pontos.csv")]
        if not cfgs or any(c not in CONFIGS for c in cfgs):
            continue
        repos = ler(rotulo + "_" + cfgs)
        pontos = [r for r in pontos if r["config"] not in cfgs] + repos
        print(f"{rotulo}: configuracoes {cfgs} substituidas por {nome}")
    return pontos


def serie(pontos, config):
    p = sorted((r for r in pontos if r["config"] == config), key=lambda r: r["triangulos"])
    return p


# ---------------------------------------------------------------------------
# Formatação pt-BR
# ---------------------------------------------------------------------------
def milhar(n):
    return f"{int(round(n)):,}".replace(",", ".")


def dec(x, casas=1):
    return f"{x:,.{casas}f}".replace(",", "X").replace(".", ",").replace("X", ".")


def mega(x, casas=2):
    return dec(x / 1e6, casas)


def fmt_eixo_tri(v, _):
    m = v / 1e6
    if m >= 1:
        return f"{m:g}".replace(".", ",")
    return f"{m:.2g}".replace(".", ",")


def fmt_eixo_num(v, _):
    return f"{v:g}".replace(".", ",")


def preparar_log(ax, xlabel="Triângulos por quadro (milhões, escala log)", ylabel="FPS médio (escala log)"):
    ax.set_xscale("log")
    ax.set_yscale("log")
    ax.xaxis.set_major_formatter(FuncFormatter(fmt_eixo_tri))
    ax.xaxis.set_minor_formatter(NullFormatter())
    ax.yaxis.set_major_locator(LogLocator(base=10, subs=(1, 2, 3, 5)))
    ax.yaxis.set_major_formatter(FuncFormatter(fmt_eixo_num))
    ax.yaxis.set_minor_formatter(NullFormatter())
    ax.grid(True, which="major", color=GRADE, linewidth=0.6)
    ax.set_xlabel(xlabel)
    ax.set_ylabel(ylabel)


def linha60(ax):
    ax.axhline(LIMITE, color=REF, linewidth=1.2, linestyle=(0, (4, 2)), zorder=1)
    ax.annotate("60 FPS", xy=(0.0, LIMITE), xycoords=("axes fraction", "data"), xytext=(4, 3),
                textcoords="offset points", ha="left", va="bottom", color=REF, fontsize=8)


def primeiro_abaixo(pts):
    abaixo = [r for r in pts if r["fps"] < LIMITE]
    return min(abaixo, key=lambda r: r["triangulos"]) if abaixo else None


def plotar_fps(ax, pontos, configs, marcar=True, rotular_limite=True, sufixo=""):
    for c in configs:
        p = serie(pontos, c)
        if not p:
            continue
        x = [r["triangulos"] for r in p]
        y = [r["fps"] for r in p]
        ax.plot(x, y, TRACO[c], color=COR[c], marker=MARCADOR[c], label=NOME[c] + sufixo, zorder=3,
                markeredgecolor="white", markeredgewidth=0.6)
        if marcar:
            b = primeiro_abaixo(p)
            if b:
                ax.plot([b["triangulos"]], [b["fps"]], marker="o", markersize=10, markerfacecolor="none",
                        markeredgecolor=COR[c], markeredgewidth=1.6, zorder=4)
                if rotular_limite:
                    ax.annotate(f"{c}: {mega(b['triangulos'])} M\n{dec(b['fps'])} FPS",
                                xy=(b["triangulos"], b["fps"]), xytext=(-10, -22), textcoords="offset points",
                                fontsize=7.5, color=TINTA, ha="right")


def salvar(fig, nome):
    os.makedirs(SAIDA_G, exist_ok=True)
    fig.tight_layout()
    fig.savefig(os.path.join(SAIDA_G, nome + ".pdf"), bbox_inches="tight")
    fig.savefig(os.path.join(SAIDA_G, nome + ".png"), bbox_inches="tight")
    plt.close(fig)
    print("grafico", nome)


# ---------------------------------------------------------------------------
# Gráficos
# ---------------------------------------------------------------------------
def graficos(e1, e2, e3, e4):
    nota = "Marcador vazado: primeiro ponto medido com FPS médio < 60."
    # 1) Configuração A
    fig, ax = plt.subplots(figsize=(6.4, 4.0))
    preparar_log(ax)
    plotar_fps(ax, e1, "A")
    linha60(ax)
    ax.set_title("FPS × triângulos por quadro — configuração A (sem iluminação, sem textura)")
    ax.legend(loc="lower left", bbox_to_anchor=(0.0, 0.26), title=nota, title_fontsize=7.5)
    salvar(fig, "g1_fps_A")

    # 2) Iluminação: A, B, C
    fig, ax = plt.subplots(figsize=(6.4, 4.0))
    preparar_log(ax)
    plotar_fps(ax, e1, "ABC", rotular_limite=False)
    linha60(ax)
    ax.set_title("FPS × triângulos — modos de iluminação (sem textura): A, B e C")
    ax.legend(loc="lower left", bbox_to_anchor=(0.0, 0.26), title=nota, title_fontsize=7.5)
    salvar(fig, "g2_fps_iluminacao")

    # 3) Textura: pares equivalentes (A×D, B×E, C×F)
    fig, axs = plt.subplots(1, 3, figsize=(10, 3.6), sharey=True)
    for ax, par, titulo in zip(axs, ("AD", "BE", "CF"), ("Sem iluminação", "Omnidirecional", "Spot")):
        preparar_log(ax, ylabel="FPS médio (escala log)" if par == "AD" else "")
        plotar_fps(ax, e1, par, rotular_limite=False)
        linha60(ax)
        ax.set_title(f"{titulo}: {par[0]} (sem) × {par[1]} (com textura)")
        ax.legend(loc="lower left", bbox_to_anchor=(0.0, 0.26))
    salvar(fig, "g3_fps_textura")

    # 4) Seis configurações + detalhe próximo ao limite (escala linear)
    fig, axs = plt.subplots(1, 2, figsize=(10, 4.0), gridspec_kw={"width_ratios": [1.25, 1]})
    preparar_log(axs[0])
    plotar_fps(axs[0], e1, CONFIGS, rotular_limite=False)
    linha60(axs[0])
    axs[0].set_title("Seis configurações (A a F)")
    axs[0].legend(loc="lower left", bbox_to_anchor=(0.0, 0.26), title=nota, title_fontsize=7.5)
    ax = axs[1]
    perto = [r for r in e1 if 40 <= r["fps"] <= 110]
    for c in CONFIGS:
        p = [r for r in serie(e1, c) if r in perto]
        if p:
            ax.plot([r["triangulos"] / 1e6 for r in p], [r["fps"] for r in p], TRACO[c], color=COR[c],
                    marker=MARCADOR[c], label=c, markeredgecolor="white", markeredgewidth=0.6)
            ax.errorbar([r["triangulos"] / 1e6 for r in p], [r["fps"] for r in p], yerr=[r["fps_dp"] for r in p],
                        fmt="none", ecolor=COR[c], elinewidth=0.8, capsize=2)
            b = primeiro_abaixo(serie(e1, c))
            if b:
                ax.plot([b["triangulos"] / 1e6], [b["fps"]], "o", markersize=10, markerfacecolor="none",
                        markeredgecolor=COR[c], markeredgewidth=1.6)
    linha60(ax)
    ax.grid(True, color=GRADE, linewidth=0.6)
    ax.xaxis.set_major_formatter(FuncFormatter(fmt_eixo_num))
    ax.set_xlabel("Triângulos por quadro (milhões)")
    ax.set_ylabel("FPS médio (barras: ±1 desvio-padrão)")
    ax.set_title("Detalhe da transição (40 a 110 FPS)")
    ax.legend(ncol=3, loc="upper right")
    salvar(fig, "g4_fps_seis_configuracoes")

    # 5) CPU × triângulos (VBO e modo imediato)
    fig, axs = plt.subplots(1, 2, figsize=(10, 3.8), sharey=True)
    for ax, dados, titulo in ((axs[0], e1, "E1: VBO (GPU dedicada)"), (axs[1], e2, "E2: modo imediato")):
        for c in "AF":
            p = serie(dados, c)
            if not p:
                continue
            x = [r["triangulos"] for r in p]
            ax.plot(x, [r["cpu_thread_max"] for r in p], TRACO[c], color=COR[c], marker="o",
                    label=f"{c}: thread mais ocupada (% de 1 núcleo)", markeredgecolor="white", markeredgewidth=0.6)
            ax.plot(x, [r["cpu_thread"] for r in p], TRACO[c], color=COR[c], marker="s", alpha=0.55,
                    label=f"{c}: thread principal (% de 1 núcleo)", markeredgecolor="white", markeredgewidth=0.6)
            ax.plot(x, [r["cpu_proc"] for r in p], TRACO[c], color=COR[c], marker="^", alpha=0.35,
                    label=f"{c}: processo (% da CPU inteira, 22 threads)", markeredgecolor="white", markeredgewidth=0.6)
        ax.set_xscale("log")
        ax.xaxis.set_major_formatter(FuncFormatter(fmt_eixo_tri))
        ax.xaxis.set_minor_formatter(NullFormatter())
        ax.set_ylim(0, 105)
        ax.grid(True, color=GRADE, linewidth=0.6)
        ax.set_xlabel("Triângulos por quadro (milhões, escala log)")
        ax.set_title(titulo)
    axs[0].set_ylabel("Utilização da CPU (%)")
    axs[1].legend(loc="center left", bbox_to_anchor=(1.0, 0.5), fontsize=7.5)
    salvar(fig, "g5_cpu")

    # 6) GPU × triângulos (utilização do motor 3D e NVML)
    fig, axs = plt.subplots(1, 2, figsize=(10, 3.8), sharey=True)
    ax = axs[0]
    for c in CONFIGS:
        p = serie(e1, c)
        if p:
            ax.plot([r["triangulos"] for r in p], [r["gpu_nv_proc"] for r in p], TRACO[c], color=COR[c],
                    marker=MARCADOR[c], label=c, markeredgecolor="white", markeredgewidth=0.6)
    p = serie(e1, "A")
    if p:
        ax.plot([r["triangulos"] for r in p], [r["gpu_intel_sis"] for r in p], ":", color=TINTA2,
                label="Intel (sistema), em A")
    ax.set_title("E1: GPU NVIDIA — motor 3D usado pelo processo")
    ax = axs[1]
    for dados, nome, cor, mk in ((e2, "E2 imediato (NVIDIA)", COR["B"], "s"), (e3, "E3 nº de orbes (NVIDIA)", COR["C"], "^"),
                                 (e4, "E4 integrada (Intel)", COR["F"], "o")):
        p = serie(dados, "A")
        if not p:
            continue
        chave = "gpu_intel_proc" if dados is e4 else "gpu_nv_proc"
        ax.plot([r["triangulos"] for r in p], [r[chave] for r in p], "-", color=cor, marker=mk, label=nome + ", config A",
                markeredgecolor="white", markeredgewidth=0.6)
    ax.set_title("Demais experimentos — motor 3D da GPU que executa")
    for ax in axs:
        ax.set_xscale("log")
        ax.xaxis.set_major_formatter(FuncFormatter(fmt_eixo_tri))
        ax.xaxis.set_minor_formatter(NullFormatter())
        ax.set_ylim(0, 105)
        ax.grid(True, color=GRADE, linewidth=0.6)
        ax.set_xlabel("Triângulos por quadro (milhões, escala log)")
        ax.legend(loc="center right", fontsize=7.5, ncol=1)
    axs[0].set_ylabel("Utilização da GPU (%)")
    salvar(fig, "g6_gpu")

    # 7) Potência e clock da GPU NVIDIA (E1, config A e F) - dois gráficos, uma escala cada
    fig, axs = plt.subplots(1, 2, figsize=(10, 3.4))
    for c in "AF":
        p = serie(e1, c)
        if not p:
            continue
        x = [r["triangulos"] for r in p]
        axs[0].plot(x, [r["nv_pot_w"] for r in p], TRACO[c], color=COR[c], marker=MARCADOR[c], label=NOME[c],
                    markeredgecolor="white", markeredgewidth=0.6)
        axs[1].plot(x, [r["nv_clk_sm"] for r in p], TRACO[c], color=COR[c], marker=MARCADOR[c], label=NOME[c],
                    markeredgecolor="white", markeredgewidth=0.6)
        lim = [r for r in p if "potencia" in str(r["motivos_reducao"])]
        if lim:
            axs[1].plot([r["triangulos"] for r in lim], [r["nv_clk_sm"] for r in lim], "x", color=TINTA, markersize=6,
                        label="limite de potência ativo" if c == "A" else None)
    axs[0].set_ylabel("Potência da GPU (W)")
    axs[0].set_title("E1: potência média da GPU NVIDIA")
    axs[1].set_ylabel("Clock dos multiprocessadores (MHz)")
    axs[1].set_title("E1: clock médio da GPU NVIDIA")
    for ax in axs:
        ax.set_xscale("log")
        ax.xaxis.set_major_formatter(FuncFormatter(fmt_eixo_tri))
        ax.xaxis.set_minor_formatter(NullFormatter())
        ax.grid(True, color=GRADE, linewidth=0.6)
        ax.set_xlabel("Triângulos por quadro (milhões, escala log)")
        ax.legend(fontsize=7.5)
    salvar(fig, "g7_potencia_clock")

    # 8) Decomposição do tempo por quadro (A) nos quatro experimentos
    fig, axs = plt.subplots(2, 2, figsize=(10, 6.4))
    for ax, dados, titulo, var in ((axs[0][0], e1, "E1: VBO, GPU dedicada", "T"), (axs[0][1], e2, "E2: modo imediato", "T"),
                                   (axs[1][0], e3, "E3: nº de orbes (T = 8)", "N"), (axs[1][1], e4, "E4: VBO, GPU integrada", "T")):
        p = serie(dados, "A")
        if not p:
            ax.set_visible(False)
            continue
        x = [r["triangulos"] for r in p]
        ax.plot(x, [r["quadro_ms"] for r in p], "-", color=TINTA, marker="o", label="tempo por quadro (1000/FPS)")
        ax.plot(x, [r["gpu_ms"] for r in p], "--", color=COR["A"], marker="s", label="tempo de GPU (GL_TIME_ELAPSED)")
        ax.plot(x, [r["cpu_ativa_ms"] for r in p], "--", color=COR["B"], marker="^",
                label="CPU ativa da thread principal")
        ax.plot(x, [r["submissao_ms"] for r in p], ":", color=COR["C"], marker="v", label="emissão dos comandos (relógio)")
        ax.axhline(1000 / LIMITE, color=REF, linewidth=1.2, linestyle=(0, (4, 2)))
        ax.annotate("16,7 ms (60 FPS)", xy=(0.0, 1000 / LIMITE), xycoords=("axes fraction", "data"), xytext=(4, 3),
                    textcoords="offset points", color=REF, fontsize=7.5)
        ax.set_xscale("log")
        ax.set_yscale("log")
        ax.xaxis.set_major_formatter(FuncFormatter(fmt_eixo_tri))
        ax.xaxis.set_minor_formatter(NullFormatter())
        ax.yaxis.set_major_locator(LogLocator(base=10, subs=(1, 2, 5)))
        ax.yaxis.set_major_formatter(FuncFormatter(fmt_eixo_num))
        ax.yaxis.set_minor_formatter(NullFormatter())
        ax.grid(True, color=GRADE, linewidth=0.6)
        ax.set_title(titulo + " — config A")
        ax.set_xlabel("Triângulos por quadro (milhões, escala log)")
        ax.set_ylabel("ms por quadro (escala log)")
    axs[0][0].legend(fontsize=7.5, loc="lower right")
    salvar(fig, "g8_tempos_quadro")

    # 9) Comparação dos regimes: config A em E1..E4
    fig, ax = plt.subplots(figsize=(8.4, 4.0))
    preparar_log(ax)
    for dados, nome, cor, mk in ((e1, "E1: VBO, RTX 4060", COR["A"], "o"), (e2, "E2: modo imediato, RTX 4060", COR["B"], "s"),
                                 (e3, "E3: nº de orbes (T = 8), RTX 4060", COR["C"], "^"),
                                 (e4, "E4: VBO, Intel Arc integrada", COR["F"], "D")):
        p = serie(dados, "A")
        if not p:
            continue
        ax.plot([r["triangulos"] for r in p], [r["fps"] for r in p], "-", color=cor, marker=mk, label=nome,
                markeredgecolor="white", markeredgewidth=0.6)
        b = primeiro_abaixo(p)
        if b:
            ax.plot([b["triangulos"]], [b["fps"]], "o", markersize=10, markerfacecolor="none", markeredgecolor=cor,
                    markeredgewidth=1.6)
    linha60(ax)
    ax.set_title("Configuração A nos quatro experimentos")
    ax.legend(loc="upper left", bbox_to_anchor=(1.01, 1.0), title=nota.replace(" com", "\ncom"), title_fontsize=7.5, alignment="left")
    salvar(fig, "g9_regimes")

    # 10-12) FPS A..F nos experimentos complementares
    for dados, nome, titulo, xlab in ((e2, "g10_fps_imediato", "E2: modo imediato — seis configurações", None),
                                      (e3, "g11_fps_objetos", "E3: aumento do número de orbes (T = 8) — A e F", None),
                                      (e4, "g12_fps_integrada", "E4: GPU integrada (Intel Arc) — seis configurações", None)):
        if not dados:
            continue
        fig, ax = plt.subplots(figsize=(8.4, 4.0))
        preparar_log(ax)
        plotar_fps(ax, dados, CONFIGS, rotular_limite=False)
        linha60(ax)
        ax.set_title(titulo)
        ax.legend(loc="upper left", bbox_to_anchor=(1.01, 1.0), title=nota.replace(" com", "\ncom"), title_fontsize=7.5, alignment="left")
        if dados is e3:
            sec = ax.secondary_xaxis("top", functions=(lambda t: (t - 17424) / 224, lambda n: n * 224 + 17424))
            sec.set_xlabel("número de orbes N (= chamadas de desenho − 3)")
            sec.xaxis.set_major_formatter(FuncFormatter(lambda v, _: milhar(v)))
        salvar(fig, nome)


# ---------------------------------------------------------------------------
# Tabelas LaTeX
# ---------------------------------------------------------------------------
def gargalo(r):
    """Classificação a partir das medições (a discussão no relatório justifica o critério)."""
    if r is None:
        return "--"
    gpu = max(r["gpu_nv_proc"], r["gpu_intel_proc"])
    razao_gpu = r["gpu_ms"] / r["quadro_ms"] if r["quadro_ms"] > 0 else 0
    if gpu >= 90 and razao_gpu >= 0.85:
        txt = "GPU (processamento de vértices)"
        # os motivos de redução vêm do NVML: só valem quando é a NVIDIA que está desenhando
        if "potencia" in str(r["motivos_reducao"]) and r["gpu_nv_proc"] >= 90:
            txt += "; no limite de potência"
        return txt
    if r["cpu_thread_max"] >= 85 and gpu < 60:
        return "CPU: 1 thread saturada (envio de comandos)"
    return "indefinido"


def situacao(pts):
    b = primeiro_abaixo(pts)
    if not b:
        return "limite não alcançado", None, None
    acima = [r for r in pts if r["fps"] >= LIMITE and r["triangulos"] < b["triangulos"]]
    if not acima:
        return "abaixo de 60 já na menor carga", None, b
    return "limite encontrado", max(acima, key=lambda r: r["triangulos"]), b


def tabela_sintese(dados, rotulo, legenda, configs=CONFIGS, chave_gpu="gpu_nv_proc"):
    linhas = []
    for c in configs:
        pts = serie(dados, c)
        if not pts:
            continue
        sit, a, b = situacao(pts)
        maior = max(pts, key=lambda r: r["triangulos"])
        if b:
            gpu = b[chave_gpu]
            linhas.append(
                f"{c} & {milhar(a['triangulos']) if a else '--'} ({dec(a['fps']) if a else '--'}) & "
                f"\\textbf{{{milhar(b['triangulos'])}}} & \\textbf{{{dec(b['fps'])}}} $\\pm$ {dec(b['fps_dp'], 2)} & "
                f"{dec(b['cpu_proc'])} / {dec(b['cpu_thread_max'], 0)} & {dec(gpu, 0)} & "
                f"{gargalo(b)} & {sit} \\\\")
        else:
            linhas.append(f"{c} & -- & -- (maior: {milhar(maior['triangulos'])}) & {dec(maior['fps'])} & "
                          f"{dec(maior['cpu_proc'])} / {dec(maior['cpu_thread_max'], 0)} & {dec(maior[chave_gpu], 0)} & "
                          f"{gargalo(maior)} & {sit} \\\\")
    corpo = "\n".join(linhas)
    tex = f"""\\begin{{table}}[H]
\\centering
\\caption{{{legenda}}}
\\label{{tab:sintese_{rotulo}}}
\\scriptsize
\\setlength{{\\tabcolsep}}{{3pt}}
\\begin{{tabularx}}{{\\textwidth}}{{c r r r c c L P{{1.6cm}}}}
\\toprule
Cfg. & Último $\\geq$ 60 & \\textbf{{Menor testado}} & FPS médio & CPU proc. / & GPU 3D & Gargalo & Situação \\\\
 & (FPS) & \\textbf{{$<$ 60}} & $\\pm$ dp & thread máx. (\\%) & (\\%) & (indício mais forte) & \\\\
\\midrule
{corpo}
\\bottomrule
\\end{{tabularx}}
\\end{{table}}
"""
    escrever(f"sintese_{rotulo}.tex", tex)


def tabela_pontos(dados, rotulo, legenda, var="T", chave_gpu="gpu_nv_proc", nv=True):
    linhas = []
    for c in CONFIGS:
        pts = sorted(serie(dados, c), key=lambda r: r["triangulos"])
        if not pts:
            continue
        b = primeiro_abaixo(pts)
        for r in pts:
            marca = "$\\blacktriangleright$" if r is b else ""
            v = r["T"] if var == "T" else r["objetos"]
            ref = "r" if r["etapa"] == "refino" else ""
            extra = f"{dec(r['nv_pot_w'], 0)} & {milhar(r['nv_clk_sm'])}" if nv else "-- & --"
            linhas.append(
                f"{marca}{c} & {milhar(v)}{ref} & {milhar(r['triangulos'])} & {dec(r['fps'])} & {dec(r['fps_dp'], 2)} & "
                f"{dec(r['quadro_ms'], 2)} & {dec(r['gpu_ms'], 2)} & {dec(r['cpu_ativa_ms'], 2)} & "
                f"{dec(r['cpu_proc'])} & {dec(r['cpu_thread_max'], 0)} & {dec(r[chave_gpu], 0)} & {extra} \\\\")
        linhas.append("\\midrule")
    if linhas and linhas[-1] == "\\midrule":
        linhas.pop()
    corpo = "\n".join(linhas)
    rot_var = "T" if var == "T" else "N"
    cab = (f"Cfg. & {rot_var} & Triângulos & FPS & dp & Quadro & GPU & CPU ativa & CPU proc. & Thread máx. & GPU 3D & Pot. & Clock \\\\\n"
           " & & & & & (ms) & (ms) & (ms) & (\\%) & (\\%) & (\\%) & (W) & (MHz) \\\\")
    tex = f"""{{\\scriptsize
\\setlength{{\\tabcolsep}}{{2.6pt}}
\\begin{{longtable}}{{l r r r r r r r r r r r r}}
\\caption{{{legenda}}}\\label{{tab:pontos_{rotulo}}}\\\\
\\toprule
{cab}
\\midrule
\\endfirsthead
\\toprule
{cab}
\\midrule
\\endhead
{corpo}
\\bottomrule
\\end{{longtable}}
}}
"""
    escrever(f"pontos_{rotulo}.tex", tex)


def macros(e1, e2, e3, e4):
    """Valores usados no texto do relatório (\\newcommand), para não digitar números à mão."""
    nomes = {"e1": "Vbo", "e2": "Imed", "e3": "Obj", "e4": "Int"}
    out = ["% Gerado por tools/graficos.py -- nao editar"]
    for chave, dados in (("e1", e1), ("e2", e2), ("e3", e3), ("e4", e4)):
        for c in CONFIGS:
            pts = serie(dados, c)
            if not pts:
                continue
            sit, a, b = situacao(pts)
            n = nomes[chave] + c
            if b:
                out.append(f"\\newcommand{{\\lim{n}}}{{{milhar(b['triangulos'])}}}")
                out.append(f"\\newcommand{{\\limM{n}}}{{{mega(b['triangulos'], 1)}}}")
                out.append(f"\\newcommand{{\\fps{n}}}{{{dec(b['fps'])}}}")
                out.append(f"\\newcommand{{\\gpu{n}}}{{{dec(b['gpu_nv_proc'] if chave != 'e4' else b['gpu_intel_proc'], 0)}}}")
                out.append(f"\\newcommand{{\\thr{n}}}{{{dec(b['cpu_thread_max'], 0)}}}")
                out.append(f"\\newcommand{{\\cpu{n}}}{{{dec(b['cpu_proc'])}}}")
            if a:
                out.append(f"\\newcommand{{\\acima{n}}}{{{milhar(a['triangulos'])}}}")
                out.append(f"\\newcommand{{\\fpsAcima{n}}}{{{dec(a['fps'])}}}")
            out.append(f"\\newcommand{{\\pontos{n}}}{{{len(pts)}}}")
            menor = min(pts, key=lambda r: r["triangulos"])
            out.append(f"\\newcommand{{\\fpsMenor{n}}}{{{dec(menor['fps'], 0)}}}")
    escrever("valores.tex", "\n".join(out) + "\n")


def escrever(nome, texto):
    os.makedirs(SAIDA_T, exist_ok=True)
    with open(os.path.join(SAIDA_T, nome), "w", encoding="utf-8") as f:
        f.write(texto)
    print("tabela", nome)


def main():
    e1, e2, e3, e4 = (ler_combinado(r) for r in ("e1_vbo_T", "e2_imediato_T", "e3_vbo_N", "e4_integrada_T"))
    if not e1:
        sys.exit("Sem dados em resultados/dados (execute executar_experimentos.bat).")
    graficos(e1, e2, e3, e4)
    tabela_sintese(e1, "e1", "Tabela-síntese do experimento principal E1 (GPU dedicada, VBO, aumento de T): menor "
                   "quantidade testada de triângulos com FPS médio abaixo de 60 em cada configuração.")
    tabela_sintese(e2, "e2", "Tabela-síntese do experimento E2 (modo imediato, GPU dedicada).")
    tabela_sintese(e3, "e3", "Tabela-síntese do experimento E3 (aumento do número de orbes, T = 8).", configs="AF")
    tabela_sintese(e4, "e4", "Tabela-síntese do experimento E4 (GPU integrada Intel Arc, VBO, aumento de T).",
                   chave_gpu="gpu_intel_proc")
    tabela_pontos(e1, "e1", "Todos os pontos medidos no experimento E1 (médias de 3 repetições de 10 s). "
                  "``r'' = ponto de refino; $\\blacktriangleright$ = primeiro ponto com FPS médio $<$ 60.")
    tabela_pontos(e2, "e2", "Pontos medidos no experimento E2 (modo imediato).")
    tabela_pontos(e3, "e3", "Pontos medidos no experimento E3 (N orbes com T = 8).", var="N")
    tabela_pontos(e4, "e4", "Pontos medidos no experimento E4 (GPU integrada). Potência e clock da NVML "
                  "não se aplicam.", chave_gpu="gpu_intel_proc", nv=False)
    macros(e1, e2, e3, e4)


if __name__ == "__main__":
    main()
