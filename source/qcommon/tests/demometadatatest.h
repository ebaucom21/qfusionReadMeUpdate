#ifndef WSW_7a5d4e84_282a_4eed_a98d_10aa92a3080f_H
#define WSW_7a5d4e84_282a_4eed_a98d_10aa92a3080f_H

#include <QtTest/QtTest>

class DemoMetadataTest : public QObject {
	Q_OBJECT

private slots:
	void test_write();
	void test_read();
	void test_readWritten();
};

#endif
