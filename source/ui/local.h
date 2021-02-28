#ifndef WSW_8a83c7a2_cec7_4598_94d9_3b9dd2306555_H
#define WSW_8a83c7a2_cec7_4598_94d9_3b9dd2306555_H

#include <QString>

namespace wsw { class StringView; }

namespace wsw::ui {

[[nodiscard]]
auto toStyledText( const wsw::StringView &text ) -> QString;

[[nodiscard]]
auto formatPing( int ping ) -> QByteArray;

}

#endif