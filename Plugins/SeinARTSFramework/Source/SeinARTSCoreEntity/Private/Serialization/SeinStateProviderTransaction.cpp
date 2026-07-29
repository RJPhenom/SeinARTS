#include "Serialization/SeinStateProviderTransaction.h"

int32& FSeinStateProviderTransactionScope::Depth()
{
	static int32 Value = 0;
	return Value;
}

FSeinStateProviderTransactionScope::
	FSeinStateProviderTransactionScope()
{
	check(IsInGameThread());
	check(Depth() == 0);
	++Depth();
}

FSeinStateProviderTransactionScope::
	~FSeinStateProviderTransactionScope()
{
	check(IsInGameThread());
	check(Depth() == 1);
	--Depth();
}

bool FSeinStateProviderTransactionScope::IsActive()
{
	check(IsInGameThread());
	return Depth() != 0;
}
