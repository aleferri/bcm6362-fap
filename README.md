Nel riepilogo, accanto a ogni dato c’è la fonte:

[HW] verificato da noi sull’hardware;
[blob] ricavato dal codice stock disassemblato;
[SDK] solo dagli header;
[ded.] dedotto.
Cosa sappiamo del FAP

Il core

		Fonte
Quanti	Uno solo sul 6362. Il 63168 ne ha due	[SDK]
CPU	MIPS 4KEp Release 2 (PRId 0x0001916c), big-endian, Fixed Mapping senza TLB, moltiplicatore iterativo	[HW]
Cache	I-cache da 16 KB a 4 vie, linee da 16 byte. Nessuna D-cache	[HW]
DSPRAM	Presente, 8 KB, attivata a 0x4000 (TagLo 0x4080, viene riletto 0x40a0) e testata. La dimensione viene dallo stock, non dall’hardware	[HW]
Altro	MIPS16e, EJTAG, interfaccia COP2 presente (cosa ci sia collegato non si sa), niente FPU né performance counter	[HW]
Frequenza	Sconosciuta. Il ciclo di polling fa 13,9 milioni di giri al secondo, quindi almeno circa 100 MHz	[HW]

Mappa degli indirizzi

Blocco	Visto dal 4KE	Visto dall’host via UBUS	Dimensione
Registri FAP	0xe0001000 [HW]	0x11001000 [HW]	4 KB, più i contatori DQM a +0x1000 [SDK]
QSM	0xe0004000 [HW]	0x11004000 [SDK]; lo stock usa l’alias SMISB 0x14004000 [blob]	16 KB [HW lato 4KE]
PSM	0xe0010000 [HW]	0x11010000 [blob]	32 KB [HW lato 4KE]
DPE	0xe0050000…	0x11040000	[SDK]
DDR	Tramite due traduttori: maschera, base lato 4KE, base lato host. Ignorano i bit 31 e 29, quindi kseg0 e kseg1 sono alias	—	blocco di dimensione potenza di due, allineato [HW]

I sotto-blocchi dentro i registri, come offset dalla base [SDK]:

controllo 0x000;
FIFO messaggi in uscita 0x100 e in ingresso 0x200;
DMA interno 0x300;
interfaccia token 0x400;
misure di prestazione 0x600;
message ID 0x700;
DQM controllo 0x800, controllo code 0xa00, dati code 0xc00, contatori 0x1000.

Registri di controllo usati

Offset	Registro	Fonte
0x00 / 0x04	irq_4ke maschera e stato	[SDK]
0x08 / 0x0c	irq_mips maschera e stato. Il bit 4 (MBOX_IN) segue gp_status	[HW]
0x10 / 0x14	gp maschera e stato. Bit 2: il 4KE ha scritto. Bit 3: l’host ha scritto. Entrambi write-one-to-clear	[HW]
0x28	host_mbox_in, dal 4KE all’host	[HW]
0x2c	host_mbox_out, dall’host al 4KE	[HW]
0x6c–0x80	Traduttori 1 e 2	[HW]
0x8c	soft_resets, bit 0 tiene fermo il 4KE	[HW]

Clock, reset, alimentazione e interrupt

Clock, reset, IDDQ: il bit del clock FAP è il 18, quello del reset il 13, quello dell’IDDQ l’11. Li gestiscono già i provider del kernel. [HW]
Interrupt verso l’host: linea periferica 29. [HW]
Interrupt verso il FAP: le periferiche si possono instradare al FAP con miscFapIrqMask, Extra ed Extra2. Lo stock ci instradava gli RX DMA dell’ENETSW e i DMA del SAR. [SDK]
ENETSW sul 6362: ha solo le linee RX DMA 0–3, niente TX. [SDK]

Avvio [HW]

alimentazione, clock e reset del blocco (circa 20 ms);
soft_resets a 1 per tenere fermo il 4KE;
immagine copiata nel blocco in DDR;
traduttore basso a 0, così le eccezioni a 0x80000180 cadono nel blocco;
traduttore di boot a 0x1fc00000, cioè il vettore di reset;
rilascio del 4KE.

Firmware stock

Dimensioni: blocco da 512 KB, circa 110 KB di codice e 150 KB di bss. [blob]
Avvio: al reset scrive 0x00C0FFEE nella mailbox e salta allo startup. Lo startup imposta K0 write-through, invalida la I-cache, configura la DSPRAM e ci mette lo stack. [blob]
QSM: secondo un commento dell’header, lo stock mette i descrittori RX dell’ENET nella parte alta della QSM. Quindi l’iuDMA è in grado di leggere descrittori da lì. [SDK]
Registri e memorie: il codice 4KE stock usa la QSM fino a 0xe0007f34 e la PSM fino a 0xe0017b80, in linea con le dimensioni. [blob]

Ancora ignoto

frequenza del 4KE e latenze di accesso a registri, PSM, QSM e DDR;
cosa c’è sul COP2;
semantica del DQM oltre le struct degli header;
come il 4KE raggiunge registri e SRAM dell’iuDMA dell’ENETSW (si ricava dal codice 4KE stock);
la QSM via UBUS dal lato host, verificabile col prossimo flash.
Piano di offload
Fase	Contenuto	Uscita	Stato
0	Loader, avvio, mailbox, log, test delle memorie	Il 4KE gira e parla con l’host	fatto
1	Caratterizzazione: frequenza dal registro CP0 Count, latenze di registri, PSM, QSM e DDR, stack in DSPRAM, interrupt sul 4KE (mailbox e timer)	Il budget di cicli per pacchetto, e quindi se 700–800 Mbit/s sono realistici	prossima
2	Trasporto tra host e FAP: code DQM in QSM con interrupt di “non vuoto”. In alternativa, ring nostri in PSM	Un ping che passa dalle code invece che dalla mailbox	
3	Passthrough: il FAP prende i canali iuDMA RX e TX dell’ENETSW, l’RX instradato al FAP, l’host riceve e trasmette tramite le code	Il router funziona interamente attraverso il FAP. Completamenti TX con interrupt, quindi il timer non serve più	
4	Flow offload: nf_flowtable con ndo_setup_tc(TC_SETUP_FT) sul conduit enetsw, che DSA già inoltra. Tabella dei flussi in PSM. Il FAP fa la ricerca sulla 5-tupla, la riscrittura NAT, il TTL, l’aggiornamento incrementale del checksum, il tag di porta, e trasmette direttamente. Statistiche e scadenze tornano all’host	Throughput del routing con NAT e carico CPU dell’host, con e senza offload
