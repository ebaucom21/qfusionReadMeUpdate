#ifndef WSW_STATICSTRINGTEST_H
#define WSW_STATICSTRINGTEST_H

#include <QtTest/QtTest>

class StaticStringTest : public QObject {
	Q_OBJECT

private slots:
	void test_ctor_vararg();
	void test_push_back();
	void test_pop_back();
	void test_append();
	void test_insert_container();
	void test_erase_unbound();
	void test_erase_countSpecified();
	void test_assignf();
	void test_appendf();
	void test_insertf();
};

#endif
