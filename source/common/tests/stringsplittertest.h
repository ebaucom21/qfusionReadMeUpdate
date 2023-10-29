#ifndef WSW_STRINGSPLITTERTEST_H
#define WSW_STRINGSPLITTERTEST_H

#include <QtTest/QtTest>

class StringSplitterTest : public QObject {
	Q_OBJECT

private slots:
	void test_splitByChar();
	void test_splitByChars();
	void test_splitByString();
};

#endif
