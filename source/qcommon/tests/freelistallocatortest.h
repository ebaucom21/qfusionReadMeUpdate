#ifndef WSW_fd388bc6_5100_4a70_b559_083d05e10665_H
#define WSW_fd388bc6_5100_4a70_b559_083d05e10665_H

#include <QStringList>
#include <QString>
#include <QVariant>

#include <QtTest/QtTest>

class FreelistAllocatorTest : public QObject {
	Q_OBJECT

	template <typename Allocator>
	void runAllocatorTest( Allocator &allocator );

	QVector<QVariant> m_expectedVariants;
private slots:
	void initTestCase();
	void test_memberBased();
	void test_heapBased();
};

#endif