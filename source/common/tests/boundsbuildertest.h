#ifndef WSW_BOUNDSBUILDERTEST_H
#define WSW_BOUNDSBUILDERTEST_H

#include <QtTest/QtTest>

class BoundsBuilderTest: public QObject {
	Q_OBJECT

private slots:
	void test_generic();
	void test_sse2();
	void test_sse2Specific();
	void test_addingByMask();
};

#endif
