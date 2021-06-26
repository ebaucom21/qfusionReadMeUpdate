#ifndef WSW_60e17f23_7782_416a_91fe_c6cd92850929_H
#define WSW_60e17f23_7782_416a_91fe_c6cd92850929_H

#include <QtTest/QtTest>

class UserInfoTest : public QObject {
	Q_OBJECT

private slots:
	void test_deserialize();
	void test_serialize();
};

#endif