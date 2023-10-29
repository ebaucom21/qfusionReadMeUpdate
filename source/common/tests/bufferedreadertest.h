#ifndef WSW_f4d7c1b9_6a09_4927_84d5_864c157c8deb_H
#define WSW_f4d7c1b9_6a09_4927_84d5_864c157c8deb_H

#include <QtTest/QtTest>

class BufferedReaderTest : public QObject {
	Q_OBJECT

private slots:
	void test_readToNewline();
	void test_readAfterReadToNewline();
};

#endif
