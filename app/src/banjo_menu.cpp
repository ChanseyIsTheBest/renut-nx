// banjo-nx (2026-11-19, the user: "I want the menu you open by pressing ABXYLR to have all the current items in the toml
// exposed to the user from 1.0.2"): N&B's rows on the game menu's Settings tab (blur-nx's game menu, the base's ports
// 177/178/180: app/src/game_menu.*), through its game hook (SetGameMenuRows). Every key of renut-nx 1.0.2's shipped
// renut.toml (the web installer's release/renut.toml) has a row, in the toml's own order: the language, the picture,
// the resolution (the fixed heights, dynamic resolution and its range, the TV output), the fur, 1.0.0's look, the frame
// cap, four-core mode, the logs, the controls (the per-player button maps are the menu's own Controls tabs). Each
// default is the Switch's (banjo_switch_default.h), so a key the player's file leaves out shows what the game runs with.
// A row marked * is read at the start (written for the next one). The texts are in the menu's twelve languages
// (game_menu_text.h's rows); N&B's own languages that the menu has no row for (Czech, Dutch, Norwegian, Swedish) show it
// in English.
#include "game_menu.h"
#include "game_menu_text.h"

#include <rex/cvar.h>
#include <rex/logging.h>
#include <rex/string.h>

#include <fmt/format.h>

#include <cstddef>
#include <iterator>
#include <cstdio>
#include <string>
#include <vector>

REXCVAR_DEFINE_BOOL(banjo_menu_check, false, "Banjo/Diagnostics",
                    "At start, the game menu's Settings rows checked in every language without the UI: each key of "
                    "1.0.2's renut.toml has a row, every text is there, each value is one of its row's choices "
                    "(\"[menu check]\" lines)");

namespace banjo::menu {
namespace {

using Setting = blur::GameMenuDialog::Setting;
using Choice = blur::GameMenuDialog::Choice;

enum BanjoText : int {
  L_DYNAMIC, L_HH_LOW, L_HH_HIGH, L_DK_LOW, L_DK_HIGH, L_STEPS, L_GPU_MS, L_UP_S, L_TV_OUT, L_FUR, L_PORTRAIT_FUR,
  L_PORTRAIT_MSAA, L_FONT, L_BOXES, L_CORE, L_FORWARDER, L_LOGS,
  H_DYNAMIC, H_HH_LOW, H_HH_HIGH, H_DK_LOW, H_DK_HIGH, H_STEPS, H_GPU_MS, H_UP_S, H_TV_OUT, H_FUR, H_PORTRAIT_FUR,
  H_PORTRAIT_MSAA, H_FONT, H_BOXES, H_CORE, H_FORWARDER, H_LOGS,
  H_FRAMERATE_N, H_HANDHELD_N, H_DOCKED_N, H_MSAA_N,
  C_GAME4X,
  kBanjoTextCount
};

// The rows: English, French, German, Italian, Spanish, Polish, Russian, Portuguese, Japanese, Korean, Chinese
// (Simplified), Chinese (Traditional): game_menu_text.h's order.
const char* const kText[blur::kMenuLanguages][kBanjoTextCount] = {
    {  // English
        "Dynamic resolution", "Handheld lowest", "Handheld highest", "Docked lowest", "Docked highest",
        "Resolution steps", "GPU time target", "Step-up delay", "TV output", "Fur detail", "Portrait fur detail",
        "Portrait MSAA", "Original font", "Original dialogue boxes", "4-core layout", "Forwarder ID", "Logs",
        "The 3D image's size follows the GPU's load, between the lowest and highest sizes below. Off: the handheld and "
        "docked sizes above.",
        "Dynamic resolution's smallest 3D image in handheld mode.",
        "Dynamic resolution's largest 3D image in handheld mode.",
        "Dynamic resolution's smallest 3D image on the TV.",
        "Dynamic resolution's largest 3D image on the TV.",
        "How many sizes dynamic resolution has between its lowest and highest.",
        "Dynamic resolution steps down when the GPU takes longer than this a frame (33.3 ms is 30 fps).",
        "Seconds the larger size must fit before dynamic resolution steps up again.",
        "The picture sent to the TV: 1080p draws the HUD and text at 1920x1080; 720p lets the TV scale it.",
        "The share of the fur's layers drawn on Banjo and others. Lower is faster; 100 % is the game's own.",
        "The same for the small portraits beside the dialogue.",
        "Edge smoothing of the small dialogue portraits: the game's 4x, or 2x (faster).",
        "The text in the original 1.0.0 font (Andy Bold) instead of the title update's.",
        "1.0.0's dialogue boxes (the old box with its cogs) instead of the title update's.",
        "With 4-core mode: 1 puts the audio and shader compiles on core 3 (recommended); 0 leaves core 3 to the system; "
        "2 as 1, other threads too.",
        "The program ID of a forwarder made by something other than Sphaira, for 4-core mode. Sphaira's forwarders are "
        "found by themselves.",
        "Logs in the logs folder next to the game, for a bug report. Off is faster.",
        "30 fps keeps a steady pace. 60 fps lets the game go faster where it can (uneven in busy areas). Off: no cap.",
        "The size of the 3D image in handheld mode (1152x648 by default). Lower is faster; the HUD and text stay sharp.",
        "The size of the 3D image on the TV (1536x864 by default). Lower is faster; the HUD and text stay sharp.",
        "The game's own edge smoothing (2x): smoother edges, a lower frame rate.",
        "Game's (4x)",
    },
    {  // French
        "Résolution dynamique", "Portable : minimum", "Portable : maximum", "Station : minimum", "Station : maximum",
        "Paliers de résolution", "Temps GPU visé", "Délai avant de monter", "Sortie TV", "Détail de la fourrure",
        "Fourrure des portraits", "MSAA des portraits", "Police d'origine", "Bulles de dialogue d'origine",
        "Disposition 4 cœurs", "ID du forwarder", "Journaux",
        "La taille de l'image 3D suit la charge du GPU, entre le minimum et le maximum ci-dessous. Désactivé : les "
        "tailles portable et station ci-dessus.",
        "La plus petite image 3D de la résolution dynamique en mode portable.",
        "La plus grande image 3D de la résolution dynamique en mode portable.",
        "La plus petite image 3D de la résolution dynamique sur la TV.",
        "La plus grande image 3D de la résolution dynamique sur la TV.",
        "Le nombre de tailles entre le minimum et le maximum de la résolution dynamique.",
        "La résolution dynamique baisse quand le GPU dépasse ce temps par image (33,3 ms = 30 fps).",
        "Secondes pendant lesquelles la taille supérieure doit tenir avant que la résolution dynamique remonte.",
        "L'image envoyée à la TV : en 1080p, l'interface et le texte sont dessinés en 1920x1080 ; en 720p, la TV "
        "agrandit l'image.",
        "La part des couches de fourrure dessinées sur Banjo et les autres. Moins est plus rapide ; 100 % est le jeu "
        "d'origine.",
        "La même chose pour les petits portraits à côté des dialogues.",
        "Le lissage des bords des petits portraits des dialogues : le 4x du jeu, ou 2x (plus rapide).",
        "Le texte dans la police d'origine de la version 1.0.0 (Andy Bold) au lieu de celle de la mise à jour.",
        "Les bulles de dialogue de la version 1.0.0 (l'ancienne bulle avec ses engrenages) au lieu de celles de la "
        "mise à jour.",
        "Avec le mode 4 cœurs : 1 place l'audio et la compilation des shaders sur le cœur 3 (recommandé) ; 0 laisse "
        "le cœur 3 au système ; 2 comme 1, avec les autres threads aussi.",
        "L'ID de programme d'un forwarder créé avec autre chose que Sphaira, pour le mode 4 cœurs. Les forwarders de "
        "Sphaira sont reconnus d'eux-mêmes.",
        "Des journaux dans le dossier logs à côté du jeu, pour signaler un bug. Désactivé est plus rapide.",
        "30 fps garde un rythme régulier. 60 fps laisse le jeu aller plus vite quand il le peut (irrégulier dans les "
        "zones chargées). Désactivé : pas de limite.",
        "La taille de l'image 3D en mode portable (1152x648 par défaut). Moins est plus rapide ; l'interface et le "
        "texte restent nets.",
        "La taille de l'image 3D sur la TV (1536x864 par défaut). Moins est plus rapide ; l'interface et le texte "
        "restent nets.",
        "Le lissage des bords du jeu (2x) : des bords plus lisses, une fréquence d'images plus basse.",
        "Celui du jeu (4x)",
    },
    {  // German
        "Dynamische Auflösung", "Handheld: Minimum", "Handheld: Maximum", "Dock: Minimum", "Dock: Maximum",
        "Auflösungsstufen", "GPU-Zeitziel", "Wartezeit zum Hochstufen", "TV-Ausgabe", "Felldetail", "Fell der Porträts",
        "MSAA der Porträts", "Originalschrift", "Original-Dialogfenster", "4-Kern-Aufteilung", "Forwarder-ID",
        "Protokolle",
        "Die Größe des 3D-Bilds folgt der Last der GPU, zwischen Minimum und Maximum unten. Aus: die Handheld- und "
        "Dock-Größen oben.",
        "Das kleinste 3D-Bild der dynamischen Auflösung im Handheld-Modus.",
        "Das größte 3D-Bild der dynamischen Auflösung im Handheld-Modus.",
        "Das kleinste 3D-Bild der dynamischen Auflösung am Fernseher.",
        "Das größte 3D-Bild der dynamischen Auflösung am Fernseher.",
        "Wie viele Größen die dynamische Auflösung zwischen Minimum und Maximum hat.",
        "Die dynamische Auflösung stuft herunter, wenn die GPU länger als diese Zeit pro Bild braucht (33,3 ms = 30 "
        "fps).",
        "Sekunden, die die größere Stufe passen muss, bevor die dynamische Auflösung wieder hochstuft.",
        "Das Bild für den Fernseher: 1080p zeichnet HUD und Text in 1920x1080; 720p lässt den Fernseher skalieren.",
        "Der Anteil der gezeichneten Fellschichten bei Banjo und anderen. Weniger ist schneller; 100 % ist das "
        "Original.",
        "Dasselbe für die kleinen Porträts neben den Dialogen.",
        "Kantenglättung der kleinen Dialogporträts: das 4x des Spiels oder 2x (schneller).",
        "Der Text in der Originalschrift von Version 1.0.0 (Andy Bold) statt der des Updates.",
        "Die Dialogfenster von Version 1.0.0 (das alte Fenster mit Zahnrädern) statt der des Updates.",
        "Im 4-Kern-Modus: 1 legt Audio und Shader-Kompilierung auf Kern 3 (empfohlen); 0 lässt Kern 3 dem System; 2 "
        "wie 1, auch für andere Threads.",
        "Die Programm-ID eines nicht mit Sphaira erstellten Forwarders, für den 4-Kern-Modus. Forwarder von Sphaira "
        "werden selbst erkannt.",
        "Protokolle im Ordner logs neben dem Spiel, für eine Fehlermeldung. Aus ist schneller.",
        "30 fps hält ein gleichmäßiges Tempo. 60 fps lässt das Spiel schneller laufen, wo es kann (ungleichmäßig in "
        "vollen Gebieten). Aus: keine Grenze.",
        "Die Größe des 3D-Bilds im Handheld-Modus (Standard 1152x648). Kleiner ist schneller; HUD und Text bleiben "
        "scharf.",
        "Die Größe des 3D-Bilds am Fernseher (Standard 1536x864). Kleiner ist schneller; HUD und Text bleiben scharf.",
        "Die Kantenglättung des Spiels (2x): glattere Kanten, eine niedrigere Bildrate.",
        "Wie im Spiel (4x)",
    },
    {  // Italian
        "Risoluzione dinamica", "Portatile: minima", "Portatile: massima", "TV: minima", "TV: massima",
        "Livelli di risoluzione", "Tempo GPU obiettivo", "Attesa per salire", "Uscita TV", "Dettaglio pelliccia",
        "Pelliccia dei ritratti", "MSAA dei ritratti", "Font originale", "Riquadri di dialogo originali",
        "Disposizione 4 core", "ID del forwarder", "Log",
        "La dimensione dell'immagine 3D segue il carico della GPU, tra la minima e la massima qui sotto. Off: le "
        "dimensioni portatile e TV qui sopra.",
        "L'immagine 3D più piccola della risoluzione dinamica in modalità portatile.",
        "L'immagine 3D più grande della risoluzione dinamica in modalità portatile.",
        "L'immagine 3D più piccola della risoluzione dinamica sulla TV.",
        "L'immagine 3D più grande della risoluzione dinamica sulla TV.",
        "Quante dimensioni ha la risoluzione dinamica tra la minima e la massima.",
        "La risoluzione dinamica scende quando la GPU impiega più di questo tempo per fotogramma (33,3 ms = 30 fps).",
        "Secondi in cui la dimensione maggiore deve bastare prima che la risoluzione dinamica risalga.",
        "L'immagine inviata alla TV: 1080p disegna HUD e testo a 1920x1080; 720p lascia che la TV la ridimensioni.",
        "La parte degli strati di pelliccia disegnati su Banjo e gli altri. Meno è più veloce; 100 % è quella del "
        "gioco.",
        "Lo stesso per i piccoli ritratti accanto ai dialoghi.",
        "L'antialiasing dei piccoli ritratti dei dialoghi: il 4x del gioco, o 2x (più veloce).",
        "Il testo nel font originale della versione 1.0.0 (Andy Bold) invece di quello dell'aggiornamento.",
        "I riquadri di dialogo della versione 1.0.0 (il vecchio riquadro con gli ingranaggi) invece di quelli "
        "dell'aggiornamento.",
        "Con la modalità 4 core: 1 mette l'audio e la compilazione degli shader sul core 3 (consigliato); 0 lascia il "
        "core 3 al sistema; 2 come 1, anche per gli altri thread.",
        "L'ID programma di un forwarder creato senza Sphaira, per la modalità 4 core. I forwarder di Sphaira vengono "
        "riconosciuti da soli.",
        "Log nella cartella logs accanto al gioco, per segnalare un bug. Off è più veloce.",
        "30 fps mantiene un ritmo costante. 60 fps lascia andare il gioco più veloce dove può (irregolare nelle zone "
        "affollate). Off: nessun limite.",
        "La dimensione dell'immagine 3D in modalità portatile (1152x648 di base). Più bassa è più veloce; HUD e testo "
        "restano nitidi.",
        "La dimensione dell'immagine 3D sulla TV (1536x864 di base). Più bassa è più veloce; HUD e testo restano "
        "nitidi.",
        "L'antialiasing del gioco (2x): bordi più morbidi, una frequenza dei fotogrammi più bassa.",
        "Del gioco (4x)",
    },
    {  // Spanish
        "Resolución dinámica", "Portátil: mínima", "Portátil: máxima", "Base: mínima", "Base: máxima",
        "Niveles de resolución", "Objetivo de tiempo de GPU", "Espera para subir", "Salida de TV",
        "Detalle del pelaje", "Pelaje de los retratos", "MSAA de los retratos", "Fuente original",
        "Cuadros de diálogo originales", "Reparto en 4 núcleos", "ID del forwarder", "Registros",
        "El tamaño de la imagen 3D sigue la carga de la GPU, entre la mínima y la máxima de abajo. Desactivado: los "
        "tamaños de portátil y base de arriba.",
        "La imagen 3D más pequeña de la resolución dinámica en modo portátil.",
        "La imagen 3D más grande de la resolución dinámica en modo portátil.",
        "La imagen 3D más pequeña de la resolución dinámica en la TV.",
        "La imagen 3D más grande de la resolución dinámica en la TV.",
        "Cuántos tamaños tiene la resolución dinámica entre la mínima y la máxima.",
        "La resolución dinámica baja cuando la GPU tarda más que esto por fotograma (33,3 ms = 30 fps).",
        "Segundos que el tamaño mayor debe caber antes de que la resolución dinámica vuelva a subir.",
        "La imagen enviada a la TV: 1080p dibuja la interfaz y el texto a 1920x1080; 720p deja que la TV la escale.",
        "La parte de las capas de pelaje que se dibujan en Banjo y los demás. Menos es más rápido; 100 % es el del "
        "juego.",
        "Lo mismo para los pequeños retratos junto a los diálogos.",
        "El suavizado de bordes de los pequeños retratos de los diálogos: el 4x del juego o 2x (más rápido).",
        "El texto con la fuente original de la versión 1.0.0 (Andy Bold) en lugar de la de la actualización.",
        "Los cuadros de diálogo de la versión 1.0.0 (el cuadro antiguo con engranajes) en lugar de los de la "
        "actualización.",
        "Con el modo de 4 núcleos: 1 pone el audio y la compilación de shaders en el núcleo 3 (recomendado); 0 deja el "
        "núcleo 3 al sistema; 2 como 1, también con los demás hilos.",
        "El ID de programa de un forwarder creado sin Sphaira, para el modo de 4 núcleos. Los forwarders de Sphaira se "
        "reconocen solos.",
        "Registros en la carpeta logs junto al juego, para informar de un error. Desactivado es más rápido.",
        "30 fps mantiene un ritmo constante. 60 fps deja que el juego vaya más rápido donde pueda (irregular en zonas "
        "concurridas). Desactivado: sin límite.",
        "El tamaño de la imagen 3D en modo portátil (1152x648 por defecto). Menos es más rápido; la interfaz y el texto "
        "siguen nítidos.",
        "El tamaño de la imagen 3D en la TV (1536x864 por defecto). Menos es más rápido; la interfaz y el texto siguen "
        "nítidos.",
        "El suavizado de bordes del juego (2x): bordes más suaves, menos fotogramas por segundo.",
        "El del juego (4x)",
    },
    {  // Polish
        "Rozdzielczość dynamiczna", "Przenośny: minimum", "Przenośny: maksimum", "Stacja: minimum",
        "Stacja: maksimum", "Stopnie rozdzielczości", "Docelowy czas GPU", "Czekanie przed wzrostem", "Wyjście TV",
        "Szczegóły futra", "Futro portretów", "MSAA portretów", "Oryginalna czcionka", "Oryginalne okna dialogowe",
        "Układ 4 rdzeni", "ID forwardera", "Logi",
        "Rozmiar obrazu 3D zależy od obciążenia GPU, między minimum a maksimum poniżej. Wył.: rozmiary trybu "
        "przenośnego i stacji powyżej.",
        "Najmniejszy obraz 3D rozdzielczości dynamicznej w trybie przenośnym.",
        "Największy obraz 3D rozdzielczości dynamicznej w trybie przenośnym.",
        "Najmniejszy obraz 3D rozdzielczości dynamicznej na telewizorze.",
        "Największy obraz 3D rozdzielczości dynamicznej na telewizorze.",
        "Ile rozmiarów ma rozdzielczość dynamiczna między minimum a maksimum.",
        "Rozdzielczość dynamiczna spada, gdy GPU potrzebuje na klatkę więcej niż tyle (33,3 ms = 30 fps).",
        "Sekundy, przez które większy rozmiar musi się mieścić, zanim rozdzielczość dynamiczna znów wzrośnie.",
        "Obraz wysyłany do telewizora: 1080p rysuje interfejs i tekst w 1920x1080; 720p zostawia skalowanie "
        "telewizorowi.",
        "Część rysowanych warstw futra Banjo i innych. Mniej jest szybciej; 100 % to oryginał gry.",
        "To samo dla małych portretów obok dialogów.",
        "Wygładzanie krawędzi małych portretów w dialogach: 4x gry lub 2x (szybciej).",
        "Tekst w oryginalnej czcionce wersji 1.0.0 (Andy Bold) zamiast czcionki z aktualizacji.",
        "Okna dialogowe z wersji 1.0.0 (stare okno z zębatkami) zamiast tych z aktualizacji.",
        "W trybie 4 rdzeni: 1 umieszcza dźwięk i kompilację shaderów na rdzeniu 3 (zalecane); 0 zostawia rdzeń 3 "
        "systemowi; 2 jak 1, także dla innych wątków.",
        "ID programu forwardera utworzonego bez Sphairy, dla trybu 4 rdzeni. Forwardery Sphairy są rozpoznawane same.",
        "Logi w folderze logs obok gry, do zgłoszenia błędu. Wył. jest szybciej.",
        "30 fps utrzymuje równe tempo. 60 fps pozwala grze działać szybciej, gdzie może (nierówno w zatłoczonych "
        "miejscach). Wył.: bez limitu.",
        "Rozmiar obrazu 3D w trybie przenośnym (domyślnie 1152x648). Mniej jest szybciej; interfejs i tekst zostają "
        "ostre.",
        "Rozmiar obrazu 3D na telewizorze (domyślnie 1536x864). Mniej jest szybciej; interfejs i tekst zostają ostre.",
        "Wygładzanie krawędzi gry (2x): gładsze krawędzie, mniej klatek na sekundę.",
        "Jak w grze (4x)",
    },
    {  // Russian
        "Динамическое разрешение", "Портативный: минимум", "Портативный: максимум", "Док: минимум", "Док: максимум",
        "Ступени разрешения", "Цель по времени GPU", "Задержка повышения", "Выход на ТВ", "Детализация меха",
        "Мех портретов", "MSAA портретов", "Исходный шрифт", "Исходные окна диалогов", "Распределение по 4 ядрам",
        "ID форвардера", "Журналы",
        "Размер 3D-изображения следует за нагрузкой GPU, между минимумом и максимумом ниже. Выкл.: размеры "
        "портативного режима и дока выше.",
        "Наименьшее 3D-изображение динамического разрешения в портативном режиме.",
        "Наибольшее 3D-изображение динамического разрешения в портативном режиме.",
        "Наименьшее 3D-изображение динамического разрешения на телевизоре.",
        "Наибольшее 3D-изображение динамического разрешения на телевизоре.",
        "Сколько размеров у динамического разрешения между минимумом и максимумом.",
        "Динамическое разрешение снижается, когда GPU тратит на кадр больше этого времени (33,3 мс = 30 к/с).",
        "Сколько секунд больший размер должен успевать, прежде чем динамическое разрешение снова повысится.",
        "Изображение для телевизора: 1080p рисует интерфейс и текст в 1920x1080; 720p оставляет масштабирование "
        "телевизору.",
        "Доля рисуемых слоёв меха у Банджо и других. Меньше — быстрее; 100 % — как в игре.",
        "То же для маленьких портретов рядом с диалогами.",
        "Сглаживание краёв маленьких портретов в диалогах: 4x игры или 2x (быстрее).",
        "Текст исходным шрифтом версии 1.0.0 (Andy Bold) вместо шрифта обновления.",
        "Окна диалогов версии 1.0.0 (старое окно с шестерёнками) вместо окон обновления.",
        "В 4-ядерном режиме: 1 отдаёт звук и компиляцию шейдеров ядру 3 (рекомендуется); 0 оставляет ядро 3 системе; "
        "2 как 1, и другие потоки тоже.",
        "ID программы форвардера, созданного не в Sphaira, для 4-ядерного режима. Форвардеры Sphaira распознаются "
        "сами.",
        "Журналы в папке logs рядом с игрой, для отчёта об ошибке. Выкл. — быстрее.",
        "30 к/с держит ровный темп. 60 к/с даёт игре идти быстрее, где возможно (неровно в людных местах). Выкл.: без "
        "ограничения.",
        "Размер 3D-изображения в портативном режиме (по умолчанию 1152x648). Меньше — быстрее; интерфейс и текст "
        "остаются чёткими.",
        "Размер 3D-изображения на телевизоре (по умолчанию 1536x864). Меньше — быстрее; интерфейс и текст остаются "
        "чёткими.",
        "Сглаживание краёв самой игры (2x): более гладкие края, ниже частота кадров.",
        "Как в игре (4x)",
    },
    {  // Portuguese
        "Resolução dinâmica", "Portátil: mínima", "Portátil: máxima", "TV: mínima", "TV: máxima",
        "Níveis de resolução", "Meta de tempo da GPU", "Espera para subir", "Saída de TV", "Detalhe do pelo",
        "Pelo dos retratos", "MSAA dos retratos", "Fonte original", "Caixas de diálogo originais",
        "Divisão em 4 núcleos", "ID do forwarder", "Registros",
        "O tamanho da imagem 3D acompanha a carga da GPU, entre a mínima e a máxima abaixo. Desligado: os tamanhos "
        "portátil e TV acima.",
        "A menor imagem 3D da resolução dinâmica no modo portátil.",
        "A maior imagem 3D da resolução dinâmica no modo portátil.",
        "A menor imagem 3D da resolução dinâmica na TV.",
        "A maior imagem 3D da resolução dinâmica na TV.",
        "Quantos tamanhos a resolução dinâmica tem entre a mínima e a máxima.",
        "A resolução dinâmica desce quando a GPU leva mais do que isso por quadro (33,3 ms = 30 fps).",
        "Segundos em que o tamanho maior precisa caber antes de a resolução dinâmica subir de novo.",
        "A imagem enviada à TV: 1080p desenha a interface e o texto em 1920x1080; 720p deixa a TV ampliar.",
        "A parte das camadas de pelo desenhadas em Banjo e nos outros. Menos é mais rápido; 100 % é o do jogo.",
        "O mesmo para os pequenos retratos ao lado dos diálogos.",
        "A suavização das bordas dos pequenos retratos dos diálogos: o 4x do jogo ou 2x (mais rápido).",
        "O texto na fonte original da versão 1.0.0 (Andy Bold) em vez da fonte da atualização.",
        "As caixas de diálogo da versão 1.0.0 (a caixa antiga com engrenagens) em vez das da atualização.",
        "Com o modo de 4 núcleos: 1 coloca o áudio e a compilação de shaders no núcleo 3 (recomendado); 0 deixa o "
        "núcleo 3 para o sistema; 2 como 1, também para as outras threads.",
        "O ID de programa de um forwarder criado sem o Sphaira, para o modo de 4 núcleos. Os forwarders do Sphaira são "
        "reconhecidos sozinhos.",
        "Registros na pasta logs ao lado do jogo, para relatar um erro. Desligado é mais rápido.",
        "30 fps mantém um ritmo estável. 60 fps deixa o jogo ir mais rápido onde puder (irregular em áreas "
        "movimentadas). Desligado: sem limite.",
        "O tamanho da imagem 3D no modo portátil (1152x648 por padrão). Menor é mais rápido; a interface e o texto "
        "continuam nítidos.",
        "O tamanho da imagem 3D na TV (1536x864 por padrão). Menor é mais rápido; a interface e o texto continuam "
        "nítidos.",
        "A suavização de bordas do próprio jogo (2x): bordas mais suaves, menos quadros por segundo.",
        "Do jogo (4x)",
    },
    {  // Japanese
        "動的解像度", "携帯モード：最小", "携帯モード：最大", "TVモード：最小", "TVモード：最大", "解像度の段階",
        "GPU時間の目標", "上げるまでの待ち時間", "TV出力", "毛並みの細かさ", "顔アイコンの毛並み", "顔アイコンのMSAA",
        "オリジナルのフォント", "オリジナルの会話ウィンドウ", "4コアの割り当て", "フォワーダーID", "ログ",
        "3D画面の大きさがGPUの負荷に合わせて、下の最小と最大の間で変わります。オフ：上の携帯モードとTVモードの大きさ。",
        "携帯モードでの動的解像度の最も小さい3D画面。",
        "携帯モードでの動的解像度の最も大きい3D画面。",
        "TVモードでの動的解像度の最も小さい3D画面。",
        "TVモードでの動的解像度の最も大きい3D画面。",
        "動的解像度の最小と最大の間にある大きさの数。",
        "GPUが1フレームにこの時間より長くかかると、動的解像度が下がります（33.3 ms = 30 fps）。",
        "動的解像度が再び上がる前に、大きいサイズが間に合い続ける必要がある秒数。",
        "TVに送る画面：1080pではHUDと文字を1920x1080で描きます。720pではTVが拡大します。",
        "バンジョーたちの毛並みを描く層の割合。少ないほど速く、100 %はゲーム本来のものです。",
        "会話の横の小さな顔アイコンの毛並み。",
        "会話の小さな顔アイコンの輪郭のなめらかさ：ゲーム本来の4x、または2x（速い）。",
        "アップデートのフォントの代わりに、バージョン1.0.0本来のフォント（Andy Bold）で文字を表示します。",
        "アップデートのものの代わりに、バージョン1.0.0の会話ウィンドウ（歯車のある古い枠）を使います。",
        "4コアモードで：1は音声とシェーダーのコンパイルをコア3に置きます（推奨）。0はコア3をシステムに残します。2は1と同じで、"
        "ほかのスレッドも使います。",
        "Sphaira以外で作ったフォワーダーのプログラムID（4コアモード用）。Sphairaのフォワーダーは自動で認識されます。",
        "不具合の報告用に、ゲームの横のlogsフォルダーにログを書きます。オフのほうが速いです。",
        "30 fpsは安定したペースを保ちます。60 fpsはできるところでゲームを速く動かします（混み合う場所では不安定）。オフ：上限なし。",
        "携帯モードの3D画面の大きさ（標準は1152x648）。小さいほど速く、HUDと文字はくっきりしたままです。",
        "TVでの3D画面の大きさ（標準は1536x864）。小さいほど速く、HUDと文字はくっきりしたままです。",
        "ゲーム本来の輪郭のなめらかさ（2x）：輪郭がなめらかになり、フレームレートが下がります。",
        "ゲーム本来（4x）",
    },
    {  // Korean
        "동적 해상도", "휴대 모드: 최소", "휴대 모드: 최대", "TV 모드: 최소", "TV 모드: 최대", "해상도 단계",
        "GPU 시간 목표", "올리기 전 대기", "TV 출력", "털 디테일", "초상화의 털", "초상화 MSAA", "원래 글꼴",
        "원래 대화 상자", "4코어 배치", "포워더 ID", "로그",
        "3D 화면 크기가 GPU 부하에 따라 아래의 최소와 최대 사이에서 바뀝니다. 끄기: 위의 휴대 모드와 TV 모드 크기.",
        "휴대 모드에서 동적 해상도의 가장 작은 3D 화면.",
        "휴대 모드에서 동적 해상도의 가장 큰 3D 화면.",
        "TV에서 동적 해상도의 가장 작은 3D 화면.",
        "TV에서 동적 해상도의 가장 큰 3D 화면.",
        "동적 해상도의 최소와 최대 사이에 있는 크기의 수.",
        "GPU가 한 프레임에 이보다 오래 걸리면 동적 해상도가 내려갑니다(33.3 ms = 30 fps).",
        "동적 해상도가 다시 올라가기 전에 더 큰 크기가 맞아야 하는 시간(초).",
        "TV로 보내는 화면: 1080p는 HUD와 글자를 1920x1080으로 그리고, 720p는 TV가 확대합니다.",
        "반조와 다른 캐릭터에 그리는 털 층의 비율. 적을수록 빠르고, 100 %는 게임 그대로입니다.",
        "대화 옆의 작은 초상화에도 똑같이 적용합니다.",
        "대화의 작은 초상화의 가장자리 다듬기: 게임의 4x 또는 2x(더 빠름).",
        "업데이트의 글꼴 대신 버전 1.0.0의 원래 글꼴(Andy Bold)로 글자를 표시합니다.",
        "업데이트의 대화 상자 대신 버전 1.0.0의 대화 상자(톱니바퀴가 있는 예전 상자)를 씁니다.",
        "4코어 모드에서: 1은 소리와 셰이더 컴파일을 코어 3에 둡니다(권장). 0은 코어 3을 시스템에 남깁니다. 2는 1과 같고 다른 "
        "스레드도 씁니다.",
        "Sphaira가 아닌 도구로 만든 포워더의 프로그램 ID(4코어 모드용). Sphaira의 포워더는 자동으로 인식됩니다.",
        "버그 신고용으로 게임 옆 logs 폴더에 로그를 씁니다. 끄면 더 빠릅니다.",
        "30 fps는 일정한 속도를 유지합니다. 60 fps는 가능한 곳에서 더 빠르게 합니다(붐비는 곳에서는 고르지 않음). 끄기: 제한 "
        "없음.",
        "휴대 모드의 3D 화면 크기(기본 1152x648). 낮을수록 빠르고 HUD와 글자는 선명하게 유지됩니다.",
        "TV의 3D 화면 크기(기본 1536x864). 낮을수록 빠르고 HUD와 글자는 선명하게 유지됩니다.",
        "게임 자체의 가장자리 다듬기(2x): 가장자리가 부드러워지고 프레임 속도는 낮아집니다.",
        "게임 그대로(4x)",
    },
    {  // Chinese (Simplified)
        "动态分辨率", "掌机：最低", "掌机：最高", "主机：最低", "主机：最高", "分辨率档位", "GPU 时间目标",
        "提升前的等待", "电视输出", "毛发细节", "头像的毛发", "头像 MSAA", "原版字体", "原版对话框", "四核分配",
        "转发器 ID", "日志",
        "3D 画面的大小随 GPU 负载变化，介于下面的最低和最高之间。关闭：使用上面的掌机和主机大小。",
        "掌机模式下动态分辨率的最小 3D 画面。",
        "掌机模式下动态分辨率的最大 3D 画面。",
        "电视上动态分辨率的最小 3D 画面。",
        "电视上动态分辨率的最大 3D 画面。",
        "动态分辨率在最低和最高之间有多少档大小。",
        "GPU 每帧用时超过此值时，动态分辨率降低（33.3 ms = 30 fps）。",
        "较大的尺寸需要持续够用多少秒，动态分辨率才会再次提升。",
        "发送到电视的画面：1080p 以 1920x1080 绘制界面和文字；720p 由电视放大。",
        "班卓等角色绘制的毛发层比例。越少越快；100 % 为游戏原样。",
        "对话旁小头像的毛发，同上。",
        "对话小头像的边缘平滑：游戏的 4x，或 2x（更快）。",
        "用 1.0.0 版的原版字体（Andy Bold）显示文字，而不是更新的字体。",
        "使用 1.0.0 版的对话框（带齿轮的旧框），而不是更新的对话框。",
        "四核模式下：1 把音频和着色器编译放在核心 3（推荐）；0 把核心 3 留给系统；2 同 1，其他线程也可使用。",
        "非 Sphaira 制作的转发器的程序 ID（用于四核模式）。Sphaira 的转发器会被自动识别。",
        "在游戏旁的 logs 文件夹写入日志，用于报告问题。关闭更快。",
        "30 fps 保持稳定的节奏。60 fps 让游戏在能做到的地方更快（在热闹的地方不稳定）。关闭：不限制。",
        "掌机模式下 3D 画面的大小（默认 1152x648）。越低越快；界面和文字保持清晰。",
        "电视上 3D 画面的大小（默认 1536x864）。越低越快；界面和文字保持清晰。",
        "游戏自身的边缘平滑（2x）：边缘更平滑，帧率更低。",
        "游戏原样（4x）",
    },
    {  // Chinese (Traditional)
        "動態解析度", "掌機：最低", "掌機：最高", "主機：最低", "主機：最高", "解析度檔位", "GPU 時間目標",
        "提升前的等待", "電視輸出", "毛髮細節", "頭像的毛髮", "頭像 MSAA", "原版字型", "原版對話框", "四核分配",
        "轉發器 ID", "記錄檔",
        "3D 畫面的大小隨 GPU 負載變化，介於下方的最低與最高之間。關閉：使用上方的掌機與主機大小。",
        "掌機模式下動態解析度的最小 3D 畫面。",
        "掌機模式下動態解析度的最大 3D 畫面。",
        "電視上動態解析度的最小 3D 畫面。",
        "電視上動態解析度的最大 3D 畫面。",
        "動態解析度在最低與最高之間有多少檔大小。",
        "GPU 每格畫面用時超過此值時，動態解析度降低（33.3 ms = 30 fps）。",
        "較大的尺寸需要持續足夠多少秒，動態解析度才會再次提升。",
        "送到電視的畫面：1080p 以 1920x1080 繪製介面與文字；720p 由電視放大。",
        "班卓等角色繪製的毛髮層比例。越少越快；100 % 為遊戲原樣。",
        "對話旁小頭像的毛髮，同上。",
        "對話小頭像的邊緣平滑：遊戲的 4x，或 2x（較快）。",
        "以 1.0.0 版的原版字型（Andy Bold）顯示文字，而非更新的字型。",
        "使用 1.0.0 版的對話框（帶齒輪的舊框），而非更新的對話框。",
        "四核模式下：1 將音訊與著色器編譯放在核心 3（建議）；0 將核心 3 留給系統；2 同 1，其他執行緒也可使用。",
        "非 Sphaira 製作的轉發器的程式 ID（用於四核模式）。Sphaira 的轉發器會自動辨識。",
        "在遊戲旁的 logs 資料夾寫入記錄檔，用於回報問題。關閉較快。",
        "30 fps 保持穩定的節奏。60 fps 讓遊戲在可行處更快（在熱鬧的地方不穩定）。關閉：不限制。",
        "掌機模式下 3D 畫面的大小（預設 1152x648）。越低越快；介面與文字保持清晰。",
        "電視上 3D 畫面的大小（預設 1536x864）。越低越快；介面與文字保持清晰。",
        "遊戲本身的邊緣平滑（2x）：邊緣更平滑，影格率較低。",
        "遊戲原樣（4x）",
    },
};

int g_row = 0;
const char* T(int id) {
  const char* s = kText[g_row][id];
  return s ? s : kText[0][id];
}

Choice C(std::string label, std::string raw, std::string toml) { return {std::move(label), std::move(raw), std::move(toml)}; }
Choice Q(std::string label, std::string raw) {  // a string key: quoted in the file
  std::string toml = "\"" + raw + "\"";
  return {std::move(label), std::move(raw), std::move(toml)};
}
std::vector<Choice> OnOff() {
  return {C(blur::GameMenuText(blur::C_ON), "true", "true"), C(blur::GameMenuText(blur::C_OFF), "false", "false")};
}
// A height as its 16:9 size (1152x648), the key's value as it is.
Choice Height(int h) {
  const std::string v = std::to_string(h);
  return C(std::to_string(h * 16 / 9) + "x" + v, v, v);
}
std::vector<Choice> Heights(int low, int high) {
  std::vector<Choice> out;
  for (int h = low; h <= high; h += 36)
    out.push_back(Height(h));
  return out;
}
// A double key: the menu reads it back as std::to_string gives it ("34.500000"), the file takes the short form.
Choice Real(const char* label, double v, const char* toml) { return C(label, std::to_string(v), toml); }

Setting Row(std::string label, std::string key, bool restart, std::vector<Choice> choices, int def, std::string help) {
  Setting x;
  x.label = std::move(label);
  x.key = std::move(key);
  x.restart = restart;
  x.choices = std::move(choices);
  x.default_choice = def;
  x.help = std::move(help);
  x.always_in_file = true;  // every one of them is a key of the shipped renut.toml
  return x;
}

int IndexOf(const std::vector<Choice>& choices, const std::string& raw) {
  for (size_t i = 0; i < choices.size(); ++i)
    if (choices[i].raw == raw)
      return int(i);
  return 0;
}

void BanjoRows(std::vector<Setting>& s, int language_row, const std::string& /*file*/) {
  g_row = language_row >= 0 && language_row < blur::kMenuLanguages ? language_row : 0;
  auto take = [&](const char* key) {  // the base's row of that key, out of the list (empty key: none)
    for (size_t i = 0; i < s.size(); ++i)
      if (s[i].key == key) {
        Setting r = std::move(s[i]);
        s.erase(s.begin() + std::ptrdiff_t(i));
        return r;
      }
    return Setting{};
  };
  std::vector<Setting> out;
  auto keep = [&](Setting r) {
    if (!r.key.empty())
      out.push_back(std::move(r));
  };

  // ---- Language: N&B's own (title_update.cpp kLanguages), by their own names; "system" first.
  {
    Setting r = take("language");
    r.choices = {Q(blur::GameMenuText(blur::C_SYSTEM), "system"), Q("English", "english"),
                 Q("English (UK)", "englishuk"), Q("Français", "french"), Q("Deutsch", "german"),
                 Q("Español", "spanish"), Q("Italiano", "italian"), Q("日本語", "japanese"), Q("한국어", "korean"),
                 Q("繁體中文", "tradchinese"), Q("简体中文", "simpchinese"), Q("Русский", "russian"),
                 Q("Polski", "polish"), Q("Čeština", "czech"), Q("Nederlands", "dutch"), Q("Norsk", "norwegian"),
                 Q("Svenska", "swedish")};
    r.default_choice = 0;
    keep(std::move(r));
  }
  // ---- Picture: N&B's msaa_level is the game's own 2x or off (renut_engine/hooks.cpp), read at the next start.
  {
    Setting r = take("msaa_level");
    r.choices = {C(blur::GameMenuText(blur::C_OFF), "0", "0"), C("2x", "2", "2")};
    r.default_choice = 0;
    r.restart = true;
    r.apply_on_close = false;
    r.help = T(H_MSAA_N);
    keep(std::move(r));
  }
  keep(take("banjo_renut_smaa"));
  // ---- Resolution: the fixed heights (1.0.2's 648 / 864), dynamic resolution and its range, the TV output.
  {
    Setting r = take("banjo_renut_render_height_handheld");
    if (!r.key.empty()) {
      r.default_choice = IndexOf(r.choices, "648");
      r.help = T(H_HANDHELD_N);
      keep(std::move(r));
    }
    Setting d = take("banjo_renut_render_height_docked");
    if (!d.key.empty()) {
      d.default_choice = IndexOf(d.choices, "864");
      d.help = T(H_DOCKED_N);
      keep(std::move(d));
    }
  }
  out.push_back(Row(T(L_DYNAMIC), "banjo_renut_dynamic_resolution", true, OnOff(), 0, T(H_DYNAMIC)));
  {
    std::vector<Choice> hh = Heights(360, 720), dk = Heights(540, 1080);
    out.push_back(Row(T(L_HH_LOW), "banjo_renut_dynamic_handheld_low", true, hh, IndexOf(hh, "540"), T(H_HH_LOW)));
    out.push_back(Row(T(L_HH_HIGH), "banjo_renut_dynamic_handheld_high", true, hh, IndexOf(hh, "720"), T(H_HH_HIGH)));
    out.push_back(Row(T(L_DK_LOW), "banjo_renut_dynamic_docked_low", true, dk, IndexOf(dk, "540"), T(H_DK_LOW)));
    out.push_back(Row(T(L_DK_HIGH), "banjo_renut_dynamic_docked_high", true, dk, IndexOf(dk, "1080"), T(H_DK_HIGH)));
  }
  {
    std::vector<Choice> steps;
    for (int n = 0; n <= 8; ++n)  // 0: only the two ends (the shipped toml's comment)
      steps.push_back(C(std::to_string(n), std::to_string(n), std::to_string(n)));
    out.push_back(Row(T(L_STEPS), "banjo_renut_dynamic_steps", true, steps, 4, T(H_STEPS)));  // 4
  }
  out.push_back(Row(T(L_GPU_MS), "banjo_renut_dynamic_gpu_ms", true,
                    {Real("30.0 ms", 30.0, "30.0"), Real("33.3 ms", 33.3, "33.3"), Real("34.5 ms", 34.5, "34.5"),
                     Real("40.0 ms", 40.0, "40.0"), Real("50.0 ms", 50.0, "50.0")},
                    2, T(H_GPU_MS)));  // 34.5
  out.push_back(Row(T(L_UP_S), "banjo_renut_dynamic_up_s", true,
                    {Real("0.5 s", 0.5, "0.5"), Real("1 s", 1.0, "1.0"), Real("2 s", 2.0, "2.0"),
                     Real("5 s", 5.0, "5.0"), Real("10 s", 10.0, "10.0")},
                    1, T(H_UP_S)));  // 1.0
  out.push_back(Row(T(L_TV_OUT), "banjo_renut_display_height_docked", true,
                    {C("720p", "720", "720"), C("900p", "900", "900"), C("1080p", "1080", "1080")}, 2, T(H_TV_OUT)));
  // ---- Fur: the shells' share (read at each fur draw: at once), the portraits' MSAA (read at the start).
  {
    std::vector<Choice> pct;
    for (int p : {25, 50, 75, 100})
      pct.push_back(C(std::to_string(p) + " %", std::to_string(p), std::to_string(p)));
    out.push_back(Row(T(L_FUR), "banjo_renut_fur_shells_percent", false, pct, 2, T(H_FUR)));                   // 75
    out.push_back(Row(T(L_PORTRAIT_FUR), "banjo_renut_portrait_fur_shells_percent", false, pct, 1, T(H_PORTRAIT_FUR)));  // 50
    out.push_back(Row(T(L_PORTRAIT_MSAA), "banjo_renut_portrait_msaa_samples", true,
                      {C(T(C_GAME4X), "0", "0"), C("2x", "2", "2"), C(blur::GameMenuText(blur::C_OFF), "1", "1")}, 1,
                      T(H_PORTRAIT_MSAA)));  // 2 (1: none, the shipped toml's comment)
  }
  // ---- 1.0.0's look (title_update.cpp, read at the start).
  out.push_back(Row(T(L_FONT), "banjo_font_andy_bold", true, OnOff(), 0, T(H_FONT)));
  out.push_back(Row(T(L_BOXES), "banjo_dialogue_boxes_original", true, OnOff(), 0, T(H_BOXES)));
  // ---- Frame rate (1.0.2's 30 fps by default).
  {
    Setting r = take("frame_cap");
    if (!r.key.empty()) {
      r.default_choice = IndexOf(r.choices, "30");
      r.help = T(H_FRAMERATE_N);
      keep(std::move(r));
    }
  }
  keep(take("input_rumble"));
  // ---- Four-core mode: the base's switch, N&B's layout and forwarder id (threading_switch.cpp, read at the start).
  keep(take("four_core_mode"));
  out.push_back(Row(T(L_CORE), "banjo_system_core", true,
                    {C("0", "0", "0"), C("1", "1", "1"), C("2", "2", "2")}, 1, T(H_CORE)));
  out.push_back(Row(T(L_FORWARDER), "banjo_forwarder_program_id", true,
                    {Q("05D6EA1333487000", "05D6EA1333487000"), Q(blur::GameMenuText(blur::C_OFF), "")}, 0,
                    T(H_FORWARDER)));
  // ---- Logs (1.0.2's toml has the key commented out: off).
  {
    Setting r = Row(T(L_LOGS), "log_level", true,
                    {Q(blur::GameMenuText(blur::C_OFF), "off"), Q(blur::GameMenuText(blur::C_ON), "info")}, 0,
                    T(H_LOGS));
    r.always_in_file = false;  // written only when on, as the shipped file has it
    out.push_back(std::move(r));
  }
  // ---- Controls and the menu itself: the base's rows as they are, in their order.
  for (Setting& r : s)
    out.push_back(std::move(r));
  s = std::move(out);
}

// The keys of renut-nx 1.0.2's shipped renut.toml (banjo-nx-installer/release/renut.toml), the per-player maps aside.
const char* const kShippedKeys[] = {
    "language", "msaa_level", "banjo_renut_smaa", "banjo_renut_dynamic_resolution",
    "banjo_renut_dynamic_handheld_low", "banjo_renut_dynamic_handheld_high", "banjo_renut_dynamic_docked_low",
    "banjo_renut_dynamic_docked_high", "banjo_renut_dynamic_steps", "banjo_renut_dynamic_gpu_ms",
    "banjo_renut_dynamic_up_s", "banjo_renut_render_height_handheld", "banjo_renut_render_height_docked",
    "banjo_renut_display_height_docked", "banjo_renut_fur_shells_percent", "banjo_renut_portrait_fur_shells_percent",
    "banjo_renut_portrait_msaa_samples", "banjo_font_andy_bold", "banjo_dialogue_boxes_original", "frame_cap",
    "banjo_system_core", "banjo_forwarder_program_id", "log_level", "input_xbox_layout", "input_joycon_sideways"};

// banjo_menu_check: the rows in every language row, without the UI: each 1.0.2 key has a row, every label, help and
// choice text is there, each default is one of the row's choices, and each key's value now is one of them.
void CheckMenuRows(const std::string& file) {
  int problems = 0;
  for (int lang = 0; lang < blur::kMenuLanguages; ++lang) {
    const std::vector<Setting> rows = blur::GameMenuSettingsRows(lang, file);
    for (const Setting& r : rows) {
      bool bad = r.label.empty() || r.help.empty() || r.choices.empty() || r.default_choice < 0 ||
                 r.default_choice >= int(r.choices.size());
      for (const Choice& c : r.choices)
        bad = bad || c.label.empty();
      if (bad && ++problems <= 16)
        REXLOG_WARN("[menu check] language row {}: {} has an empty text or a default outside its choices", lang, r.key);
    }
    if (lang != 0)
      continue;
    int found = 0, matching = 0;
    std::string missing, unmatched;
    for (const char* key : kShippedKeys) {
      bool have = false;
      for (const Setting& r : rows)
        have = have || r.key == key;
      if (have)
        ++found;
      else
        missing += std::string(missing.empty() ? "" : ", ") + key;
    }
    for (const Setting& r : rows) {
      const std::string raw = blur::GameMenuCurrentRaw(r);
      bool hit = false;
      for (const Choice& c : r.choices)
        hit = hit || rex::string::compare_case(c.raw.c_str(), raw.c_str()) == 0;
      if (hit)
        ++matching;
      else
        unmatched += fmt::format("{}{} = \"{}\"", unmatched.empty() ? "" : ", ", r.key, raw);
    }
    REXLOG_INFO("[menu check] {} Settings rows; {} of {} keys of 1.0.2's renut.toml have a row{}{}; {} rows show the "
                "value the game runs with as one of their choices{}{}",
                rows.size(), found, std::size(kShippedKeys), missing.empty() ? "" : " (missing: ", missing.empty() ? "" : missing + ")",
                matching, unmatched.empty() ? "" : " (not one of the choices: ", unmatched.empty() ? "" : unmatched + ")");
    std::string order;
    for (const Setting& r : rows)
      order += fmt::format("{}{}{}", order.empty() ? "" : " | ", r.label, r.restart ? " *" : "");
    REXLOG_INFO("[menu check] the rows (English): {}", order);
  }
  REXLOG_INFO("[menu check] {} language rows checked: {}", blur::kMenuLanguages,
              problems ? fmt::format("{} problems (above)", problems) : std::string("every text present"));
}

}  // namespace

void InstallMenuRows(const std::string& file) {
  blur::SetGameMenuRows(&BanjoRows);
  if (REXCVAR_GET(banjo_menu_check))
    CheckMenuRows(file);
}

}  // namespace banjo::menu
