#include "freelistallocatortest.h"
#include "../freelistallocator.h"

static constexpr unsigned kCapacity = 32;

void FreelistAllocatorTest::initTestCase() {
	for( unsigned i = 0; i < kCapacity; ++i ) {
		if( i % 2 ) {
			QVariantList list;
			for( unsigned j = 0; j < kCapacity; ++j ) {
				list.append( QString::number( j ).repeated( j ) );
			}
			m_expectedVariants.append( list );
		} else {
			m_expectedVariants.append( QString::number( i ) );
		}
	}
}

template <typename Allocator>
void FreelistAllocatorTest::runAllocatorTest( Allocator &allocator ) {
	assert( allocator.capacity() == m_expectedVariants.size() );

	// Check whether it does not break after a full alloc/free cycle
	for( unsigned attemptNum = 0; attemptNum < 8; ++attemptNum ) {
		QVector<QVariant *> constructedVariants;
		for( unsigned i = 0; i < kCapacity; ++i ) {
			constructedVariants.push_back( new( allocator.allocOrThrow() )QVariant( m_expectedVariants[i] ) );
		}

		bool failedToAlloc = false;
		try {
			(void)allocator.allocOrThrow();
		} catch( ... ) {
			failedToAlloc = true;
		}

		QVERIFY( failedToAlloc );

		for( unsigned i = 0; i < kCapacity; ++i ) {
			QCOMPARE( *constructedVariants[i], m_expectedVariants[i] );
		}

		// Alter the free() calls order (LIFO, FIFO)
		if( attemptNum % 2 ) {
			for( int i = 0; i < constructedVariants.size(); ++i ) {
				QVariant *variant = constructedVariants[i];
				variant->~QVariant();
				allocator.free( variant );
				// Make sure other items remain untouched
				for( int j = i + 1; j != constructedVariants.size(); ++j ) {
					QCOMPARE( *constructedVariants[j], m_expectedVariants[j] );
				}
			}
		} else {
			for( int i = constructedVariants.size() - 1; i >= 0; --i ) {
				QVariant *variant = constructedVariants[i];
				variant->~QVariant();
				allocator.free( variant );
				// Make sure other items remain untouched
				for( int j = 0; j < i; ++j ) {
					QCOMPARE( *constructedVariants[j], m_expectedVariants[j] );
				}
			}
		}
	}
}

void FreelistAllocatorTest::test_memberBased() {
	wsw::MemberBasedFreelistAllocator<sizeof( QVariant ), kCapacity, 8> allocator1;
	runAllocatorTest<decltype( allocator1 )>( allocator1 );
	wsw::MemberBasedFreelistAllocator<sizeof( QVariant ), kCapacity, 16> allocator2;
	runAllocatorTest<decltype( allocator2 )>( allocator2 );
	wsw::MemberBasedFreelistAllocator<sizeof( QVariant ), kCapacity, 32> allocator3;
	runAllocatorTest<decltype( allocator3 )>( allocator3 );
	wsw::MemberBasedFreelistAllocator<sizeof( QVariant ), kCapacity, 64> allocator4;
	runAllocatorTest<decltype( allocator4 )>( allocator4 );
	wsw::MemberBasedFreelistAllocator<sizeof( QVariant ), kCapacity, 4096> allocator5;
	runAllocatorTest<decltype( allocator5 )>( allocator5 );
}

void FreelistAllocatorTest::test_heapBased() {
	for( unsigned alignment: { 8, 16, 32, 64, 4096 } ) {
		wsw::HeapBasedFreelistAllocator allocator( sizeof( QVariant ), kCapacity, alignment );
		runAllocatorTest<decltype( allocator )>( allocator );
	}
}

