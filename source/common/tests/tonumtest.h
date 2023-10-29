#ifndef WSW_TONUMTEST_H
#define WSW_TONUMTEST_H

#include <QtTest/QtTest>

class ToNumTest : public QObject {
	Q_OBJECT

private slots:
	void test_parse_bool();
	void test_parse_int();
	void test_parse_unsigned();
	void test_parse_int64();
	void test_parse_uint64();
	void test_parse_float();
	void test_parse_double();
	void test_parse_longDouble();
};

#endif
