# LogS – Lagerorganisation Software

Eine einfache Lagerverwaltungssoftware für Windows. LogS speichert Artikel,
Lagerplätze, Mengen, Preise, Bilder und Programmeinstellungen in einer einzelnen
SQLite-Datenbank.

LogS ist hauptsächlich für die private Verwaltung kleiner Lagerbestände
gedacht. Es ist keine Mehrbenutzer- oder Unternehmenslösung.

Das Programm ist als eigenständige Windows-Anwendung ausgeführt und benötigt
weder .NET noch Java. Unter Linux kann es mit Wine verwendet werden.

Der aktuelle Entwicklungsstand ist **Version 1.0.117**.

## Funktionen

- Verwaltung von Artikeln, Mengen, Einheiten und Preisen
- Eindeutige Lagernummer für jeden Artikel
- Angabe von Lagerort und Fach
- Suche über alle Text-, Zahlen- und Datumsangaben eines Artikels
- Speicherung von Artikelbildern direkt in der Datenbank
- Heller und dunkler Anzeigemodus
- Anpassbare Bezeichnungen für EAN / UPC / GTIN, Lagerort und Fach
- Erstellung und Druck von Code-128-Barcodes und QR-Codes
- Optionales LogS-Logo in QR-Codes
- Scannerfenster für USB-Scanner, die sich wie eine Tastatur verhalten
- Filterung der Artikelliste durch gescannte Fachnummern
- Einzelner Etikettendruck oder automatisch gefüllte DIN-A4-Seiten
- Automatische Erkennung eines angeschlossenen Brother-Etikettendruckers
- CSV-Export und Kopieren der Datenbank über „Speichern unter“
- Neue Datenbanken können direkt im Programm erstellt werden

## Voraussetzungen

- Windows 10 oder Windows 11 in der 64-Bit-Version
- alternativ Linux mit einer aktuellen Wine-Version
- ein installierter Druckertreiber für den Etiketten- oder Seitendruck

Für den direkten Brother-Etikettendruck unter Wine wird zusätzlich
`ptouch-print` benötigt. Normale Windows-Drucker und der DIN-A4-Druck laufen
weiterhin über die Druckerauswahl von Windows beziehungsweise Wine.

Für die fertige Version sind keine zusätzlichen Laufzeitumgebungen oder
Bibliotheken erforderlich.

## Vorkompilierte Version verwenden

Für den normalen Betrieb wird nur `LogS.exe` benötigt. Lege die Datei in einen
eigenen Ordner und starte sie mit einem Doppelklick.

Wenn sich noch keine Datenbank im selben Ordner befindet, erstellt LogS beim
ersten Start automatisch `logs.db`. Danach besteht eine vollständige Installation
aus diesen beiden Dateien:

- `LogS.exe` – das Programm
- `logs.db` – Artikel und Einstellungen

Die Datenbank darf umbenannt werden. Befindet sich genau eine Datei mit der
Endung `.db` neben dem Programm, wird sie beim Start automatisch geöffnet.

## Verwendung

Trage mindestens einen Artikelnamen ein und ergänze die gewünschten Angaben.
Mit **Eintrag speichern** wird der Artikel in die geöffnete Datenbank übernommen.

Die Lagernummer bezeichnet einen eindeutigen Platz. Sie kann deshalb nicht zwei
verschiedenen Artikeln gleichzeitig zugewiesen werden. Beim Überschreiben einer
bereits verwendeten Lagernummer fragt LogS vorher nach.

Über die Schaltfläche neben einem leeren Feld kann LogS die erste freie Lager-
oder Fachnummer einsetzen. Die Nummern verwenden sechs Stellen, zum Beispiel
`L-000001` und `F-000001`.

Ein gespeicherter Artikel kann über die Tabelle ausgewählt, bearbeitet oder
gelöscht werden. Die Schaltflächen **Ältester Eintrag** und **Neuester Eintrag**
springen direkt zum entsprechenden Datensatz.

## Bilder

Mit **Bild hinzufügen** kann einem Artikel ein Bild zugewiesen werden. Das Bild
wird auf 1024 × 1024 Pixel angepasst, als PNG gespeichert und direkt in die
SQLite-Datenbank geschrieben.

Dadurch sind keine getrennten Bildordner notwendig. Über **Bild speichern** kann
das gespeicherte Bild wieder als Datei exportiert werden.

## Barcodes und QR-Codes

Die kleine Code-Schaltfläche neben der Lagernummer öffnet den Druckbereich. Der
aktuelle Wert kann als Code-128-Barcode oder QR-Code ausgegeben werden. Die
Lagernummer wird zusätzlich als lesbarer Text unter dem Code gedruckt.

Der Bereich **Etikettendrucker** druckt ein einzelnes Etikett. Bandbreite und
Etikettenlänge werden in Millimetern angegeben. Passt ein Code nicht lesbar auf
die gewählte Länge, wird der Druck abgelehnt. Die Anzeige im Fenster zeigt, ob
ein Brother-Etikettendrucker erkannt wurde.

Der Bereich **DIN A4** erstellt fortlaufend nummerierte Codes und verteilt sie
automatisch auf die verfügbare Seite. Breite und Höhe werden in Millimetern
angegeben.

Unter Windows wird ein installierter Brother-Drucker automatisch gesucht. Unter
Wine verwendet LogS `ptouch-print`. Der automatische Schnitt hängt vom jeweiligen
Druckermodell ab. Beim Brother PT-1950 muss das Etikett manuell geschnitten werden.

## Scanner

Das Fenster **Direkteingabe / Scannen** wartet auf die Eingabe eines Scanners.
Wird eine Lagernummer erkannt, öffnet LogS den passenden Artikel. Eine gescannte
Fachnummer zeigt alle Artikel aus diesem Fach. Bei einer anschließenden
Lagernummer oder einer Suche wird wieder die vollständige Liste verwendet.

## Datenbank und Sicherung

Artikel werden beim Speichern direkt in die geöffnete Datenbank geschrieben.
Noch nicht gespeicherte Änderungen im Eingabeformular erkennt LogS trotzdem.
Über
**Datenbank → Datenbank speichern unter…** kann eine vollständige Kopie erstellt
werden. Diese Datei enthält auch Bilder und Programmeinstellungen.

Beim Wechsel der Datenbank oder beim Schließen des Programms warnt LogS, wenn
der aktuell bearbeitete Eintrag noch nicht gespeichert wurde.

## Aus dem Quellcode bauen

Das Build-Skript verwendet einen 64-Bit-MinGW-Cross-Compiler und die im Repository
enthaltene SQLite-Amalgamation. Unter Debian oder Ubuntu können die benötigten
Compiler-Werkzeuge so installiert werden:

```sh
sudo apt install g++-mingw-w64-x86-64 binutils-mingw-w64-x86-64
```

Alternativ erkennt das Skript LLVM-MinGW, wenn es systemweit installiert ist.
Eine vorhandene lokale LLVM-MinGW-Installation unter
`work/toolchain/opt/llvm-mingw` wird ebenfalls automatisch verwendet.

Starte den Build anschließend im Projektordner mit:

```sh
./build.sh
```

Die fertige `LogS.exe` wird im Ordner `outputs` abgelegt. Eine Datenbank gehört
nicht zum Build-Ergebnis: Wenn neben der EXE noch keine `.db`-Datei liegt, erstellt
LogS sie beim ersten Start selbst.

## Fehlerbehebung

Wenn LogS unter Wine nicht startet, prüfe zuerst die verwendete Wine-Version und
starte das Programm aus einem Terminal, um Fehlermeldungen zu sehen:

```sh
wine LogS.exe
```

Wenn unter Wine kein Brother-Drucker erkannt wird, prüfe zunächst:

```sh
ptouch-print --info
```

Der Befehl muss den angeschlossenen Drucker und die eingelegte Bandbreite melden.

Bei Problemen mit einer Datenbank sollte vor weiteren Änderungen eine Kopie der
betroffenen `.db`-Datei angelegt werden.

## Autor

LogS wird von **@pms** entwickelt.

## Lizenz

Der eigene Programmcode von LogS steht unter der **PolyForm Noncommercial
License 1.0.0**. LogS darf für nicht kommerzielle Zwecke kostenlos verwendet,
verändert und weitergegeben werden. Eine kommerzielle Nutzung ist nicht
gestattet. Der vollständige Lizenztext steht in der Datei `LICENSE`.

LogS ist damit öffentlich einsehbare, nicht kommerziell lizenzierte Software,
aber keine Open-Source-Software nach der Definition der Open Source Initiative.

Hinweise zu verwendeten Fremdkomponenten stehen in
`THIRD_PARTY_NOTICES.txt`.
