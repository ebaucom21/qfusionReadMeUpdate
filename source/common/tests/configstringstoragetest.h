#ifndef WSW_3ac3f7c0_6967_403e_ba0c_9d04598392fa_H
#define WSW_3ac3f7c0_6967_403e_ba0c_9d04598392fa_H

#include <QtTest/QtTest>

class ConfigStringStorageTest : public QObject {
	Q_OBJECT

private slots:
	void test_setEmptyString();
	void test_setShortString();
	void test_setShortString_overShortString();
	void test_setLongString();
	void test_setLongString_overShortString();
	void test_setLongString_overLongString();
	void test_setShortString_overLongString();
	void test_clear();
	void test_copyFrom();
};

#endif
