#ifndef WSW_61ac089b_20b8_4a63_b2bd_0e93a59b5efe_H
#define WSW_61ac089b_20b8_4a63_b2bd_0e93a59b5efe_H

/**
 * A common superclass for objects usually provided as dependencies to other objects.
 * Selection objects should have valid/invalid states and "instance ids"
 * (so different instantiations can be distinguished).
 */
class Selection {
public:
	virtual ~Selection() = default;
	/**
	 * Returns an instance id that helps to make a distinction
	 * between two instances of the same object
	 * (even if {@code ValidForSelection} call returns true for both instances).
	 * Instance ids they just have to be same for same instances.
	 * The opposite is not true: different instances may have the same id,
	 * but this should only happen if an instance counter wraps,
	 * and given the implied short lifetime of instances, this should never occur.
	 * If an instance has been invalidated (but has not been made valid again),
	 * it should preserve its id.
	 */
	virtual unsigned InstanceId() const = 0;
	/**
	 * Sometimes the most obvious name "IsValid()" looks ugly
	 * if the actual ancestor is based on a plural world, and also
	 * this trivial name might clash with "IsValid"() call that
	 * might have more specific meaning for a concrete ancestor.
	 */
	virtual bool ValidAsSelection() const = 0;
};

#endif
