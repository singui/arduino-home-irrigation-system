# arduino-home-irrigation-system by sini or singui

/*
   RIEGO DEL TECHO VERDE DE LAS HABITACIONES

   Voy a contar con (Versión 0.1)
    - 2 sensores de humedad (HL-69 - 3.3-5v) que me van a decir la humedad del suelo para activar o no la bomba
    - 1 rele de estado sólido (?? - 5v / 220v) que va a controlar
    - 1 bomba de lavarropas conectada a 220v
    Versión 0.2
    - 1 caudalímetro (a comprar) que me va a saber decir cuánta agua le tiré al suelo
    - 1 sensor de luz (a comprar) que me va a permitir saber qué tan fuerte está el sol

*/


/*
   Lógica:
   Seteo los sensores, rele, etc...
   1) Chequeo la intensidad del sol con la luz, devuelvo si es hora de ver si necesita agua o no Hora de riego (true) / No hora de riego (false):
      a. Para la versión 0.1 no voy a tener sensor, así que voy a consumir una API con la hora y voy a regar en invierno y otoño de 18 a 20 hs.
        En verano y primavera por la mañana y/o por la tardecita: 19-21 hs.
      b. Versión 0.2, voy a tener sensor de luz... según la intensidad riego o no (ver esto)
   2) Leo la humedad de cada sensor de humedad. Me va a devolver si necesita agua o no: NecesitaAgua (true) / NecesitaAgua (false). Voy a controlar
   que ninguno de los dos pase un máximo de humedad.
   3) Si HoraRegar y NecesitaAgua son verdaderos entonces prendo la bomba durante 5 minutos. Vuelvo a chequear a los 15 minutos la humedad y repito el proceso.
      a. Versión 0.2: mido el caudal regado por la bomba

   CONTROLES
   1) Cada 30 minutos registro la humedad del suelo y guardo la fecha con día y horario, la humedad detectada, bajo riego (false)
   2) Después de regar, ejecuto este proceso cada 5 minutos hasta 3 horas después del último riego: mido la humedad y guardo con bajo riego (true)

*/
