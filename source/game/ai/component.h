#ifndef WSW_6ea080fc_332b_4b1a_a464_6315a731f70a_H
#define WSW_6ea080fc_332b_4b1a_a464_6315a731f70a_H

#include "ailocal.h"
#include <stdarg.h>

class AiComponent {
	char tag[64];
protected:
#ifndef _MSC_VER
	void SetTag( const char *format, ... ) __attribute__( ( format( printf, 2, 3 ) ) );
	void Debug( const char *format, ... ) const __attribute__( ( format( printf, 2, 3 ) ) );
	[[noreturn]] void FailWith( const char *format, ... ) const __attribute__( ( format( printf, 2, 3 ) ) );
#else
	void SetTag( _Printf_format_string_ const char *format, ... );
	void Debug( _Printf_format_string_ const char *format, ... ) const;
	[[noreturn]] void FailWith( _Printf_format_string_ const char *format, ... ) const;
#endif
public:
	AiComponent() {
		// TODO: Always require providing a valid tag
		tag[0] = '\0';
	}

	const char *Tag() const { return tag; }

	virtual ~AiComponent() = default;
};

class AiFrameAwareComponent : public AiComponent {
protected:
	unsigned frameAffinityModulo { 0 };
	unsigned frameAffinityOffset { 0 };

	bool ShouldSkipThinkFrame() {
		// Check whether the modulo has not been set yet
		// TODO: Add protection against never set modulo
		return frameAffinityModulo == 0 || (unsigned)( level.framenum % frameAffinityModulo ) != frameAffinityOffset;
	}

	virtual void Frame() {}
	virtual void PreFrame() {}
	virtual void PostFrame() {}

	void CheckIsInThinkFrame( const char *function ) {
#ifdef _DEBUG
		if( !ShouldSkipThinkFrame() ) {
			return;
		}
		const char *tag = "AiFrameAwareComponent::CheckIsInThinkFrame()";
		const char *format = "%s has been called not in think frame: frame#=%" PRId64 ", modulo=%d, offset=%d\n";
		AI_FailWith( tag, format, function, level.framenum, frameAffinityModulo, frameAffinityOffset );
#endif
	}

	virtual void Think() {}
	virtual void PreThink() {}
	virtual void PostThink() {}

	// May be overridden if some actions should be performed when a frame affinity is set
	virtual void SetFrameAffinity( unsigned modulo, unsigned offset ) {
		assert( modulo && offset < modulo );
		frameAffinityModulo = modulo;
		frameAffinityOffset = offset;
	}
public:
	// Call this method to update an instance state.
	// TODO: Rename to Run() or something like that.
	void Update();
};

#endif
