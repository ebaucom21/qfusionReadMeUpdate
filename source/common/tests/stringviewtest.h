#ifndef WSW_STRINGVIEWTEST_H
#define WSW_STRINGVIEWTEST_H

#include <QtTest/QtTest>

class StringViewTest : public QObject {
	Q_OBJECT

private slots:
	void test_constructor_default();
	void test_constructor_charPtr();
	void test_constructor_3Args();

	void test_equalsIgnoreCase();

	void test_maybeFront();
	void test_maybeBack();
	void test_maybeAt();

	void test_indexOf_char();
	void test_lastIndexOf_char();
	void test_indexOf_charLookup();
	void test_lastIndexOf_charLookup();
	void test_indexOf_view();
	void test_lastIndexOf_view();

	void test_contains_char();
	void test_contains_view();
	void test_containsAny();
	void test_containsOnly();
	void test_containsAll();

	void test_startsWith_char();
	void test_endsWith_char();
	void test_startsWith_view();
	void test_endsWith_view();

	void test_trimLeft_noArgs();
	void test_trimLeft_char();
	void test_trimLeft_chars();

	void test_trimRight_noArgs();
	void test_trimRight_char();
	void test_trimRight_chars();

	void test_trim_noArgs();
	void test_trim_char();
	void test_trim_chars();

	void test_take();
	void test_takeExact();
	void test_takeWhile();

	void test_drop();
	void test_dropExact();
	void test_dropWhile();

	void test_takeRight();
	void test_takeRightExact();
	void test_takeRightWhile();

	void test_dropRight();
	void test_dropRightExact();
	void test_dropRightWhile();

	void test_takeMid();
	void test_takeMidExact();

	void test_dropMid();
	void test_dropMidExact();
};

#endif
