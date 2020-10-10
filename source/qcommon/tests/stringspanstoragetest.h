#ifndef WSW_a02f757e_9494_4f60_afed_5c044e972401_H
#define WSW_a02f757e_9494_4f60_afed_5c044e972401_H

#include <QtTest/QtTest>

class StringSpanStorageTest : public QObject {
	Q_OBJECT

private slots:
	void test_stdBasedStorage();
	void test_qtBasedStorage();
	void test_staticBasedStorage();
private:
	template <typename Storage>
	void performStorageTest( Storage &s );
};

#endif