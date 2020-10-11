#ifndef WSW_a02f757e_9494_4f60_afed_5c044e972401_H
#define WSW_a02f757e_9494_4f60_afed_5c044e972401_H

#include <QtTest/QtTest>

#ifndef Q_strnicmp
#define Q_strnicmp strncasecmp
#endif

#include "../wswstringview.h"

class StringSpanStorageTest : public QObject {
	Q_OBJECT

private slots:
	void initTestCase();

	void test_stdBasedStorage();
	void test_qtBasedStorage();
	void test_staticBasedStorage();
private:
	template <typename Storage>
	void performGenericTest( Storage &storage );

	template <typename Storage>
	void performPopBackTest( Storage &storage );

	std::vector<std::string> m_referenceStringsStorage;
	std::vector<wsw::StringView> m_referenceStrings;
};

#endif