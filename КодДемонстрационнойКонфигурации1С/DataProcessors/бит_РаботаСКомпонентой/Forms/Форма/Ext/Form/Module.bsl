﻿&НаКлиенте
Перем ПараметрыСервера;

&НаКлиенте
Процедура ЗапуститьКомпонентуВФоновомЗадании(Команда)	
	бит_КомпонентаSynchClientServerСервер.ЗапуститьСерверВФоновомЗадании();
КонецПроцедуры

&НаКлиенте
Процедура ОстановитьФоновоеЗаданиеКомпоненты(Команда)
	бит_КомпонентаSynchClientServerСервер.УдалитьФоновоеЗаданиеСервера();
КонецПроцедуры

&НаКлиенте
Процедура ЗапуститьКомпонентуНаКлиенте(Команда)
	
	ПараметрыСервера = бит_КомпонентаSynchClientServerКлиентСервер.ИнициализироватьСервер();	
	Если ПараметрыСервера = Неопределено Тогда
		ТекстОшибки = "Сервер не может быть запущен. Параметры сервера не определены";
		бит_КомпонентаSynchClientServerСервер.ДобавитьЗаписьЛога(ТекстОшибки, "ERROR");
		ОтключитьОбработчикОжидания("ОбработатьНовыеСообщения");
		
		Возврат;
	КонецЕсли;
	
	ПодключитьОбработчикОжидания("ОбработатьНовыеСообщения", 1);
	
КонецПроцедуры

&НаКлиенте
Процедура ОбработатьНовыеСообщения() Экспорт
	
	бит_КомпонентаSynchClientServerКлиентСервер.ОбработатьНовыеСообщения(
		ПараметрыСервера.КомпонентаСервер, ПараметрыСервера.ПараметрыРаботыСервера
	);
	
КонецПроцедуры

