// OpenFOAM RTS under MSVC/clang-cl: template addToRunTimeSelectionTable uses
// Derived::typeName as the default key, but that word lives in a different
// COMDAT than the registrar. The registrar often runs while typeName is still
// empty, so the table only has "". Lookup then fails even if the .C was
// compiled into this archive.
//
// shyx_snappy_run calls this after all TU static constructors, and we insert
// with an explicit Foam::word key, which bypasses typeName.

#include "includeEntry.H"
#include "wallPolyPatch.H"
#include "wallFvPatch.H"
#include "wallPointPatch.H"
#include "triSurfaceMesh.H"
#include "noDecomp.H"

#include "slipPointPatchFields.H"
#include "fixedValuePointPatchFields.H"
#include "calculatedPointPatchFields.H"
#include "zeroGradientPointPatchFields.H"
#include "cyclicSlipPointPatchFields.H"
#include "zeroFixedValuePointPatchFields.H"

#include "fixedValueFvPatchFields.H"
#include "calculatedFvPatchFields.H"
#include "zeroGradientFvPatchFields.H"
#include "slipFvPatchFields.H"

#include "calculatedFvsPatchFields.H"
#include "fixedValueFvsPatchFields.H"

#include "medialAxisMeshMover.H"

#include "gaussGrad.H"
#include "gaussLaplacianScheme.H"
#include "linear.H"
#include "correctedSnGrad.H"
#include "EulerDdtScheme.H"

namespace
{
template<class Base, class Derived>
void registerPatchField(const char* key)
{
    const Foam::word k(key);
    static typename Base::template addpatchConstructorToTable<Derived> patchCtor(k);
    static typename Base::template addpatchMapperConstructorToTable<Derived> mapperCtor(k);
    static typename Base::template adddictionaryConstructorToTable<Derived> dictCtor(k);
}

template<class Base, class Derived>
void registerIstreamScheme(const char* key)
{
    const Foam::word k(key);
    static typename Base::template addIstreamConstructorToTable<Derived> ctor(k);
}

template<class Base, class Derived>
void registerMeshScheme(const char* key)
{
    const Foam::word k(key);
    static typename Base::template addMeshConstructorToTable<Derived> ctor(k);
}

template<class Base, class Derived>
void registerInterpolation(const char* key)
{
    const Foam::word k(key);
    static typename Base::template addMeshConstructorToTable<Derived> mesh(k);
    static typename Base::template addMeshFluxConstructorToTable<Derived> flux(k);
}

template<class Type>
void registerStandardPatchFields()
{
    using Point = Foam::pointPatchField<Type>;
    using Fv = Foam::fvPatchField<Type>;
    using Fvs = Foam::fvsPatchField<Type>;

    registerPatchField<Point, Foam::slipPointPatchField<Type>>("slip");
    registerPatchField<Point, Foam::fixedValuePointPatchField<Type>>("fixedValue");
    registerPatchField<Point, Foam::calculatedPointPatchField<Type>>("calculated");
    registerPatchField<Point, Foam::zeroGradientPointPatchField<Type>>("zeroGradient");
    registerPatchField<Point, Foam::cyclicSlipPointPatchField<Type>>("cyclicSlip");
    registerPatchField<Point, Foam::zeroFixedValuePointPatchField<Type>>("zeroFixedValue");

    registerPatchField<Fv, Foam::slipFvPatchField<Type>>("slip");
    registerPatchField<Fv, Foam::fixedValueFvPatchField<Type>>("fixedValue");
    registerPatchField<Fv, Foam::calculatedFvPatchField<Type>>("calculated");
    registerPatchField<Fv, Foam::zeroGradientFvPatchField<Type>>("zeroGradient");

    registerPatchField<Fvs, Foam::calculatedFvsPatchField<Type>>("calculated");
    registerPatchField<Fvs, Foam::fixedValueFvsPatchField<Type>>("fixedValue");
}
} // namespace

void shyx_force_foam_rts()
{
    registerStandardPatchFields<Foam::scalar>();
    registerStandardPatchFields<Foam::vector>();
    registerStandardPatchFields<Foam::sphericalTensor>();
    registerStandardPatchFields<Foam::symmTensor>();
    registerStandardPatchFields<Foam::tensor>();

    {
        const Foam::word k("medialAxisMeshMover");
        static Foam::externalDisplacementMeshMover::adddictionaryConstructorToTable<
            Foam::medialAxisMeshMover>
            medial(k);
    }

    registerIstreamScheme<Foam::fv::gradScheme<Foam::scalar>, Foam::fv::gaussGrad<Foam::scalar>>(
        "Gauss");
    registerIstreamScheme<Foam::fv::gradScheme<Foam::vector>, Foam::fv::gaussGrad<Foam::vector>>(
        "Gauss");

    registerIstreamScheme<Foam::fv::laplacianScheme<Foam::scalar, Foam::scalar>,
        Foam::fv::gaussLaplacianScheme<Foam::scalar, Foam::scalar>>("Gauss");
    registerIstreamScheme<Foam::fv::laplacianScheme<Foam::vector, Foam::scalar>,
        Foam::fv::gaussLaplacianScheme<Foam::vector, Foam::scalar>>("Gauss");

    registerInterpolation<Foam::surfaceInterpolationScheme<Foam::scalar>, Foam::linear<Foam::scalar>>(
        "linear");
    registerInterpolation<Foam::surfaceInterpolationScheme<Foam::vector>, Foam::linear<Foam::vector>>(
        "linear");

    registerMeshScheme<Foam::fv::snGradScheme<Foam::scalar>, Foam::fv::correctedSnGrad<Foam::scalar>>(
        "corrected");
    registerMeshScheme<Foam::fv::snGradScheme<Foam::vector>, Foam::fv::correctedSnGrad<Foam::vector>>(
        "corrected");

    registerIstreamScheme<Foam::fv::ddtScheme<Foam::scalar>, Foam::fv::EulerDdtScheme<Foam::scalar>>(
        "Euler");
    registerIstreamScheme<Foam::fv::ddtScheme<Foam::vector>, Foam::fv::EulerDdtScheme<Foam::vector>>(
        "Euler");

    volatile const void* keep = &Foam::functionEntries::includeEntry::log;
    keep = &Foam::wallPolyPatch::typeName;
    keep = &Foam::wallFvPatch::typeName;
    keep = &Foam::wallPointPatch::typeName;
    keep = &Foam::triSurfaceMesh::typeName;
    keep = &Foam::noDecomp::typeName;
    (void)keep;
}
