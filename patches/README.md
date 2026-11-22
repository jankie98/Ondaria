# Correzioni a Tracktion Engine

Il motore è un sottomodulo git fermo a una versione precisa. Le correzioni che servono a
Ondaria e che il motore non ha ancora stanno qui come patch, e CMake le applica al
sottomodulo durante la configurazione, se non sono già applicate.

## tracktion-timestretch-allineamento.patch

Le clip con lo stretch (warp) non cadevano sul beat quando la velocità era diversa da 1.
Con un loop a 120 BPM in un progetto a 60 BPM ogni colpo arrivava circa 0,1 s in anticipo.
Le cause erano quattro:

- **Velocità sbagliata all'avvio.** Lo stretcher si "riscaldava" a velocità 1, prima che il
  nodo impostasse quella vera. Ora il riscaldamento avviene alla prima lettura.
- **Latenza di Signalsmith a velocità diversa da 1.** Metà della latenza d'uscita si allunga
  con l'audio. Il valore è stato misurato: entro ~10 ms da 0,5x a 2x.
- **Audio riletto prima del punto di partenza.** Quello che lo stretcher rilegge per
  riscaldarsi esce alla velocità di riproduzione, quindi va scartato in proporzione.
- **Marker di warp.** Il rapporto si legge a metà blocco: proprio sul primo marker valeva 1.
  Al passaggio di un marker lo stretcher si riallinea, perché la sua latenza dipende dalla
  velocità.

`tests/WarpTests.cpp` misura dove cadono i colpi nel file renderizzato e copre tutti questi
casi.

Per rigenerare la patch dopo averla modificata:

```
git -C external/tracktion_engine diff > patches/tracktion-timestretch-allineamento.patch
```
