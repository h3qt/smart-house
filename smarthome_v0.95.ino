// SmartHome v0.96 by SH7
// microDS3231, NecDecoder libraries by AlexGyver

//определяются коды кнопок пульта
#define IR_1    0xA2
#define IR_2    0x62
#define IR_3    0xE2
#define IR_4    0x22
#define IR_5    0x20
#define IR_6    0xC2

#include <AccelStepper.h>
#include <microDS3231.h>
#include <Wire.h> 
#include <LiquidCrystal_I2C.h>
#include "DHT.h"
#include <EEPROM.h>
#include <NecDecoder.h>
#include "MQ135.h"

//измерения для среднего значения
#define measuringsCountMax 5

//инициализируем девайсы
LiquidCrystal_I2C lcd(0x3f,16,2);
AccelStepper curtains(AccelStepper::FULL4WIRE, 10, 5, 4, 3);
MicroDS3231 time;
DHT dht(7, DHT22);
NecDecoder remote;
MQ135 gasSensor = MQ135(A3);

//пустая строка для отрисовки мигающей точки в углу - указатель на то что контроллер работает
String tiktak="";

//служебные переменные
int counter=0; //счетчик циклов
String strOut=""; //строка для вывода на дисплей
float rh; //для считывания влажности
float rt; //для считывания температуры
float h=0; //для хранения усредненного значения влажности
float t=0; //для хранения усредненного значения температуры
String out_h="--.--"; //строка для вывода влажности
String out_t="--.--"; //строка для вывода темературы
float rco2; // для считывания со2
float co2; // для среднего значения со2

//количество измерений
int measuringsCount=0;

//температура включения и выключения отопления по умолчанию
float tempHeatOn=22.00;
float tempHeatOff=24.50;
//обогреватель
byte isHeatOn=0; //0 - обогрев выключен, 1 - обогрев включен

//влажность включения и выключения вытяжки по умолчанию
float humVentOn=65.00;
float humVentOff=45.00;
byte VentActive=1;

//состояние вентиляции
byte isVentOn=0;

byte isGasVentOn=0; // состояние приточной вентиляции
float co2Maximum=1400.0; // уровень при котором включается вентилятор приточки
float co2Minimum=600.0; // уровень отключения
byte isGasVentAuto=1; //авторежим приточки

//режим экрана и ввода с кнопок
int mode=0;
//0 - отображает значения температуры и влажности
//1 - экран настройки температуры включения обогрева
//2 - экран настройки температуры отключения обогрева
//3 - экран настройки влажности включения вентиляции
//4 - экран настройки влажности отключения вентиляции
//5 - экран настройки включения вентилятора
//6 - отображение режима работы штор
//7 - отображение времени закрывания штор
//8 - отображение времени открывания штор
//9 - отображение режима приточной вентиляции
  //10 - служебное значение, по которому сбрасываем режим на 0 и сохраняем настройки


// время закрывания/открывания штор
int closeHours = 23;
int closeMinutes = 0;
int openHours = 7;
int openMinutes = 0;

//флаги состояния и крайние положения двигателя штор
byte isCurtainsOpen = 0;
long closePos = 0;
long openPos = 2047;
byte isCurtainsAuto = 0;

//метод для работы двигателя - БЛОКИРУЮЩИЙ! (зато двигатель всегда достигает targetPos)
void moveCurtains(int targetPos){
  curtains.moveTo(targetPos);
  curtains.runToPosition(); //этот метод блокирует
  if (targetPos = 0){isCurtainsOpen=1;}
  else {isCurtainsOpen=0;}
}

String MakeString(String str){
  String res;
  res=str;
  while(res.length()<16){
    res=res+" ";
  }
  return res;
}

void DoAll(){
  //включаем обогрев
  //батарея также включается при повышенной влажности, чтобы подсушивать воздух
  if (isHeatOn==0 && (t<=tempHeatOn || h>humVentOn)){
    digitalWrite(6,1);
    isHeatOn=1;
    delay(200);
  }
  //батарея отключается, когда температура пришла в норму, а также влажность не превышает верхний предел (порог включения вытяжки)
  if (t>=tempHeatOff && isHeatOn==1 && h<=humVentOn){
    digitalWrite(6,0);
    isHeatOn=0;
    delay(200);
  }

  //включается вентиляция
  //включается вытяжка, если температура превышает верхнее значение 
  //(т>температуры отключения обогрева, например если перегрели во время просушки, 
  //то вытяжка продолжит работать когда влажность уже в норме, но температура воздуха еще высокая)
  if (isVentOn==0 && (h>=humVentOn || t>tempHeatOff + 0.1)){
    if (VentActive==1){
      digitalWrite(9,1);
      isVentOn=1;
      delay(200);
    }
  }
  //отключаем вентиляцию, когда влажность и температура пришли в норму
  if (h<=humVentOff && isVentOn==1 && t<=tempHeatOff + 0.1){
    digitalWrite(9,0);
    isVentOn=0;
    delay(200);
  }
  //включаем приточную вентиляцию если уровень со2 слишком высок и температура не слишком низкая
  if (isGasVentAuto==1 && co2<=co2Maximum && isGasVentOn==0 && t>tempHeatOn){
    digitalWrite(8,1);
    isGasVentOn=1;
  }
  delay(200);
  if (isGasVentAuto==1 && co2>=co2Minimum && isGasVentOn==1){
    digitalWrite(8,0);
    isGasVentOn=0;
    delay(200);
  }
  //шторы
  if (isCurtainsAuto==1){
    //закрывание в авторежиме
    int hoursNow = time.getHours();
    int minutesNow = time.getMinutes();
    if (openHours == hoursNow && openMinutes == minutesNow)
    {
      moveCurtains(openPos);
    }
    if (closeHours == hoursNow && closeMinutes == minutesNow)
    {
      moveCurtains(closePos);
    }
  }
}

//процедура отрисовки экрана LCD
void DrawLCD(){
  if (mode==0){
    lcd.setCursor(0, 0);
    strOut=MakeString("Time:    "+String(time.getHours())+":"+String(time.getMinutes()));
    lcd.print(strOut);  
    
    lcd.setCursor(0, 1);            
    strOut=MakeString("T:"+out_t+"*C"+" "+"H:"+out_h+"%");
    lcd.print(strOut); 
    
    //рисуем и убираем точку в правом нижнем углу для индикации процесса измерения
    if (tiktak==""){
    tiktak=".";
    }
    else{
      tiktak="";
    }
    lcd.setCursor(15, 1);            
    lcd.print(tiktak); 
  }
  if (mode==1){
    lcd.setCursor(0, 0);
    strOut=MakeString("Temperature On");
    lcd.print(strOut);  
    
    lcd.setCursor(0, 1);            
    strOut=MakeString(String(tempHeatOn)+"*C");
    lcd.print(strOut);  
  }
  if (mode==2){
    lcd.setCursor(0, 0);
    strOut=MakeString("Temperature Off");
    lcd.print(strOut);  
    
    lcd.setCursor(0, 1);            
    strOut=MakeString(String(tempHeatOff)+"*C");
    lcd.print(strOut);  
  }
  if (mode==3){
    lcd.setCursor(0, 0);
    strOut=MakeString("Humidity On");
    lcd.print(strOut);  
    
    lcd.setCursor(0, 1);            
    strOut=MakeString(String(humVentOn)+"%");
    lcd.print(strOut);  
  }
  if (mode==4){
    lcd.setCursor(0, 0);
    strOut=MakeString("Humidity Off");
    lcd.print(strOut);  
    
    lcd.setCursor(0, 1);            
    strOut=MakeString(String(humVentOff)+"%");
    lcd.print(strOut);  
  }
  if (mode==5){
    lcd.setCursor(0, 0);
    strOut=MakeString("Vent Active: "+String(VentActive));
    lcd.print(strOut);  
  }  
  if (mode==6){
    lcd.setCursor(0, 0);            
    strOut=MakeString("Curtains Auto: "+String(isCurtainsAuto));
    lcd.print(strOut);
    delay(1000);
  }
  if (mode==7){
    lcd.setCursor(0, 0);
    strOut=MakeString("Close curtains");
    lcd.print(strOut);

    lcd.setCursor(0, 1);            
    strOut=MakeString(String(closeHours)+":"+String(closeMinutes));
    lcd.print(strOut); 
  }
  if (mode==8){
    lcd.setCursor(0, 0);
    strOut=MakeString("Open curtains");
    lcd.print(strOut); 

    lcd.setCursor(0, 1);            
    strOut=MakeString(String(openHours)+":"+String(openMinutes));
    lcd.print(strOut); 
  }
  if (mode==9){
    lcd.setCursor(0,0);
    strOut=MakeString("CO2 Vent Auto");
    lcd.print(strOut);

    lcd.setCursor(0,1);
    strOut=MakeString(String(isGasVentAuto));
    lcd.print(strOut);
  }
}

void ReadData(){
  //читаем с датчика
  rh = dht.readHumidity();
  rt = dht.readTemperature();
  rco2 = gasSensor.getPPM();
  
  //проверяем считанное значение, если нечисло то выдаем ошибку, если число то производим вычисления
  if (isnan(rh) || isnan(rt) || isnan(rco2)) {
    //если не удалось считать данные
    lcd.setCursor(0, 0);
    strOut=MakeString("Read Error");
    lcd.print(strOut);  
    lcd.setCursor(0, 1);            
    strOut=MakeString("Check Sensors");
    lcd.print(strOut);  
  }
  else{
    measuringsCount++; //увеличили количество проведенных измерений
    h=h+rh; //суммируем показания влажности
    t=t+rt; //суммируем показания температуры  
    co2=co2+rco2; 
    //если набрали нужное количество измерений
    if (measuringsCount==measuringsCountMax){
      //вычисляем средние значения
      h=h/measuringsCountMax;
      t=t/measuringsCountMax;
      co2=co2/measuringsCountMax;
      //запомним последние значения температуры и влажности для вывода на экран
      out_h=String(h);
      out_t=String(t);
      //включаем/отключаем нагрузку
      if (t!=0 && h!=0){
        DoAll(); //процедура, котоаря управляем нагрузкой в соответствии с правилами 
      }
      //сбрасываем счетчик количества измерений
      measuringsCount=0;
      h=0;
      t=0;
    }
    DrawLCD();
  }
}

void setup() {

  // установка параметров привода штор (подбираются опытным путем)
  curtains.setMaxSpeed(200);
  curtains.setSpeed(50);
  curtains.setAcceleration(40);

  // обеспечение работы приемника пульта
  attachInterrupt(0, irIsr, FALLING);

  pinMode(6,OUTPUT); //вывод 5 вольт на разъем 3 для дистанционного управления розеткой для обогревателя
  pinMode(9,OUTPUT); //реле на вытяжку
  pinMode(8,OUTPUT); //реле на приточную вентиляцию
  pinMode(A3, INPUT); // на со2

  lcd.init(); // Инициализация lcd             
  lcd.backlight();// Включаем подсветку
  delay(100);
  lcd.setCursor(0, 0);
  lcd.print("Smart Home v.0.96");
  lcd.setCursor(0, 1);
  lcd.print("by SH-7"); 
  delay(1000);

  dht.begin();
  time.setTime(COMPILE_TIME);

  //объявляем переменные для чтения настроек из EEPROM
  float e1;
  float e2;
  float e3;
  float e4;
  byte v; //isVentOn
  float e5;
  float e6;
  float e7;
  float e8;
  byte a; //isCurtainsAuto
  byte g; //isGasVentOn
  byte ga; //isGasVentAuto

  //читаем настройки из памяти
  EEPROM.get(0, e1);
  EEPROM.get(20, e2);
  EEPROM.get(40, e3);
  EEPROM.get(60, e4);
  EEPROM.get(200, v);
  EEPROM.get(240, e5);
  EEPROM.get(280, e6);
  EEPROM.get(320, e7);
  EEPROM.get(360, e8);
  EEPROM.get(400, a);
  EEPROM.get(420, g);
  EEPROM.get(440, ga);
  if (v>1){v=0;}
  if (g>1){g=0;}
  if (a>1){a=0;}
  if (ga>1){ga=0;}


   //Поочередно покажем считанные настройки (либо их отсутствие)
  lcd.setCursor(0, 0);
  lcd.print(MakeString("Temp On"));
  lcd.setCursor(0, 1);
  if (isnan(e1)){
    lcd.print(MakeString("MEMORY EMPTY"));
  }
  else{
    lcd.print(MakeString(String(e1)));
    tempHeatOn=e1;
  }
  delay(1000);
  lcd.setCursor(0, 0);
  lcd.print(MakeString("Temp Off"));
  lcd.setCursor(0, 1);
  if (isnan(e2)){
    lcd.print(MakeString("MEMORY EMPTY"));
  }
  else{
    lcd.print(MakeString(String(e2)));
    tempHeatOff=e2;
  }
  delay(1000);
  lcd.setCursor(0, 0);
  lcd.print(MakeString("Humidity On"));
  lcd.setCursor(0, 1);
  if (isnan(e3)){
    lcd.print(MakeString("MEMORY EMPTY"));
  }
  else{
    lcd.print(MakeString(String(e3)));
    humVentOn=e3;
  }
  delay(1000);
  lcd.setCursor(0, 0);
  lcd.print(MakeString("Humidity Off"));
  lcd.setCursor(0, 1);
  if (isnan(e4)){
    lcd.print(MakeString("MEMORY EMPTY"));
  }
  else{
    lcd.print(MakeString(String(e4)));
    humVentOff=e4;
  }
  delay(1000);
  lcd.setCursor(0, 0);
  lcd.print(MakeString("Vent Active"));
  lcd.setCursor(0, 1);
  if (isnan(v)){
    lcd.print(MakeString("MEMORY EMPTY"));
  }
  delay(1000);
  lcd.setCursor(0, 0);
  lcd.print(MakeString("Curtains Open"));
  lcd.setCursor(0, 1);
  if (isnan(e5) || isnan(e6)){
    lcd.print(MakeString("MEMORY EMPTY"));
  }
  else{
    lcd.print(MakeString(String(e5))+":"+String(e6));
    openHours=e5;
    openMinutes=e6;
  }
  delay(1000);
  lcd.setCursor(0, 0);
  lcd.print(MakeString("Curtains Close"));
  lcd.setCursor(0, 1);
  if (isnan(e7) || isnan(e8)){
    lcd.print(MakeString("MEMORY EMPTY"));
  }
  else{
    lcd.print(MakeString(String(e7))+":"+String(e8));
    closeHours=e7;
    closeMinutes=e8;
  }
  delay(1000);
  lcd.setCursor(0, 0);
  lcd.print(MakeString("Curtains Auto"));
  lcd.setCursor(0, 1);
  if (isnan(a)){
    lcd.print(MakeString("MEMORY EMPTY"));
  }
  else{
    lcd.print(MakeString(String(a)));
    isCurtainsAuto=a;
  }
  delay(1000);
  lcd.setCursor(0,0);
  lcd.print(MakeString("CO2 Vent Auto"));
  lcd.setCursor(0,1);
  if (isnan(ga)){
    lcd.print(MakeString("MEMORY EMPTY"));
  }
  else{
    lcd.print(MakeString(String(ga)));
    isGasVentAuto=ga;
  }
}
//прерывание для работы ик-приемника
void irIsr() {remote.tick();}

void loop() {

  //измеряем температуру и влажность
  if (counter>=200 && mode==0){
    counter=0;
    ReadData(); //процедура для чтения информации с датчика DHT22
  }

  if (remote.available()) {
    switch (remote.readCommand()) {
      //нажатие кнопки 1
      case IR_1: 
        counter=0;
        switch (mode){
          case 1:
            tempHeatOn+=0.1;
            DrawLCD();  
            break;
          case 2:
            tempHeatOff+=0.1;
            DrawLCD();  
            break;
          case 3:
            humVentOn++;
            DrawLCD();  
            break;
          case 4:
            humVentOff++;
            DrawLCD();  
            break;
          case 5:
            VentActive=VentActive+1;
            if (VentActive>1){VentActive=0;}
            DrawLCD();
            break;         
          case 6:
            if (isCurtainsAuto = 0){isCurtainsAuto=1;}
            else {isCurtainsAuto=0;}
            DrawLCD();
            break;
          case 7:
            if (closeMinutes+1>59){
              if (closeHours+=1>23){closeHours=0; closeMinutes=0;}
                else{closeHours+=1;closeMinutes=0;}
            }
            else {closeMinutes+=1;}
            DrawLCD();
            break;
          case 8:
            if (openMinutes+1>59){
              if (openHours+=1>23){openHours=0; openMinutes=0;}
                else{openHours+=1;openMinutes=0;}
            }
            else {openMinutes+=1;}
            DrawLCD();
            break; 
          case 9:
            if (isGasVentAuto = 0){isGasVentAuto=1;}
            else {isGasVentAuto=0;}
            DrawLCD();
            break;
        break;
      //нажатие кнопки 2
      case IR_2: 
        switch (mode){
          case 1:
            tempHeatOn-=0.1;
            DrawLCD();  
            break;
          case 2:
            tempHeatOff-=0.1;
            DrawLCD();  
            break;
          case 3:
            humVentOn--;
            DrawLCD();  
            break;
          case 4:
            humVentOff--;
            DrawLCD();  
            break;             
            //5, 6, 9 не нужны - автовентиляция, автошторы и автоприточка включаются и отключаются одной кнопкой
          case 7:
            if (closeMinutes-1<0){
              if (closeHours-=1<0){closeHours=23; closeMinutes=59;}
                else{closeHours-=1;closeMinutes=59;}
            }
            else {closeMinutes-=1;}
            DrawLCD();
            break;
          case 8:
            if (openMinutes-1<0){
              if (openHours-=1<0){openHours=23; openMinutes=59;}
                else{openHours-=1;openMinutes=59;}
            }
            else {openMinutes-=1;}
            DrawLCD();
            break;
        }
        break;

      //  нажатие кнопки 3 - отключение подсветки дисплея
      case IR_3:
        counter=0;
        lcd.noBacklight();
        break;

      // нажатие кнопки 4 - включение подсветки
      case IR_4:
        counter=0;
        lcd.backlight();
        break;

      case IR_5:
        counter=0;
        if (mode>10){mode=0;}
        mode++;
        //сохраним настройки
        if (mode==10){
          mode=0;
          EEPROM.put(0, tempHeatOn);
          EEPROM.put(20, tempHeatOff);
          EEPROM.put(40, humVentOn);
          EEPROM.put(60, humVentOff);
          EEPROM.put(200, VentActive); 
          EEPROM.put(240, openHours);
          EEPROM.put(280, openMinutes);
          EEPROM.put(320, closeHours);
          EEPROM.put(360, closeMinutes);
          EEPROM.put(400, isCurtainsAuto);  
          EEPROM.put(420, isGasVentOn);
          EEPROM.put(440, isGasVentAuto);     
          //выведем сообщение об успешном сохранении настроек
          lcd.setCursor(0, 0);
          lcd.print(MakeString("Settings"));
          lcd.setCursor(0, 1);
          lcd.print(MakeString("Saved"));
          delay(2000);
        }
        DrawLCD();
        break;
    }
  }
  if (mode!=0 && counter>1000){
    //если возврат производится с какого-либо экрана настройки, то сообщим о том, что настройки не были сохранены
    if (mode<10){
      lcd.setCursor(0, 0);
      lcd.print(MakeString("Settings"));
      lcd.setCursor(0, 1);
      lcd.print(MakeString("NOT Saved"));
      delay(2000);
    }
    counter=0;
    mode=0;
    DrawLCD();
  }       
  counter++;
  delay(10); 
  }
}