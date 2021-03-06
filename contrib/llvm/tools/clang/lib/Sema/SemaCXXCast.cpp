//===--- SemaNamedCast.cpp - Semantic Analysis for Named Casts ------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//  This file implements semantic analysis for C++ named casts.
//
//===----------------------------------------------------------------------===//

#include "clang/Sema/SemaInternal.h"
#include "clang/Sema/Initialization.h"
#include "clang/AST/ExprCXX.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/CXXInheritance.h"
#include "clang/Basic/PartialDiagnostic.h"
#include "llvm/ADT/SmallVector.h"
#include <set>
using namespace clang;

enum TryCastResult {
  TC_NotApplicable, ///< The cast method is not applicable.
  TC_Success,       ///< The cast method is appropriate and successful.
  TC_Failed         ///< The cast method is appropriate, but failed. A
                    ///< diagnostic has been emitted.
};

enum CastType {
  CT_Const,       ///< const_cast
  CT_Static,      ///< static_cast
  CT_Reinterpret, ///< reinterpret_cast
  CT_Dynamic,     ///< dynamic_cast
  CT_CStyle,      ///< (Type)expr
  CT_Functional   ///< Type(expr)
};

static void CheckConstCast(Sema &Self, Expr *&SrcExpr, QualType DestType,
                           const SourceRange &OpRange,
                           const SourceRange &DestRange);
static void CheckReinterpretCast(Sema &Self, Expr *&SrcExpr, QualType DestType,
                                 const SourceRange &OpRange,
                                 const SourceRange &DestRange,
                                 CastKind &Kind);
static void CheckStaticCast(Sema &Self, Expr *&SrcExpr, QualType DestType,
                            const SourceRange &OpRange,
                            CastKind &Kind,
                            CXXCastPath &BasePath);
static void CheckDynamicCast(Sema &Self, Expr *&SrcExpr, QualType DestType,
                             const SourceRange &OpRange,
                             const SourceRange &DestRange,
                             CastKind &Kind,
                             CXXCastPath &BasePath);

static bool CastsAwayConstness(Sema &Self, QualType SrcType, QualType DestType);

// The Try functions attempt a specific way of casting. If they succeed, they
// return TC_Success. If their way of casting is not appropriate for the given
// arguments, they return TC_NotApplicable and *may* set diag to a diagnostic
// to emit if no other way succeeds. If their way of casting is appropriate but
// fails, they return TC_Failed and *must* set diag; they can set it to 0 if
// they emit a specialized diagnostic.
// All diagnostics returned by these functions must expect the same three
// arguments:
// %0: Cast Type (a value from the CastType enumeration)
// %1: Source Type
// %2: Destination Type
static TryCastResult TryLValueToRValueCast(Sema &Self, Expr *SrcExpr,
                                           QualType DestType, unsigned &msg);
static TryCastResult TryStaticReferenceDowncast(Sema &Self, Expr *SrcExpr,
                                               QualType DestType, bool CStyle,
                                               const SourceRange &OpRange,
                                               unsigned &msg,
                                               CastKind &Kind,
                                               CXXCastPath &BasePath);
static TryCastResult TryStaticPointerDowncast(Sema &Self, QualType SrcType,
                                              QualType DestType, bool CStyle,
                                              const SourceRange &OpRange,
                                              unsigned &msg,
                                              CastKind &Kind,
                                              CXXCastPath &BasePath);
static TryCastResult TryStaticDowncast(Sema &Self, CanQualType SrcType,
                                       CanQualType DestType, bool CStyle,
                                       const SourceRange &OpRange,
                                       QualType OrigSrcType,
                                       QualType OrigDestType, unsigned &msg,
                                       CastKind &Kind,
                                       CXXCastPath &BasePath);
static TryCastResult TryStaticMemberPointerUpcast(Sema &Self, Expr *&SrcExpr,
                                               QualType SrcType,
                                               QualType DestType,bool CStyle,
                                               const SourceRange &OpRange,
                                               unsigned &msg,
                                               CastKind &Kind,
                                               CXXCastPath &BasePath);

static TryCastResult TryStaticImplicitCast(Sema &Self, Expr *&SrcExpr,
                                           QualType DestType, bool CStyle,
                                           const SourceRange &OpRange,
                                           unsigned &msg,
                                           CastKind &Kind);
static TryCastResult TryStaticCast(Sema &Self, Expr *&SrcExpr,
                                   QualType DestType, bool CStyle,
                                   const SourceRange &OpRange,
                                   unsigned &msg,
                                   CastKind &Kind,
                                   CXXCastPath &BasePath);
static TryCastResult TryConstCast(Sema &Self, Expr *SrcExpr, QualType DestType,
                                  bool CStyle, unsigned &msg);
static TryCastResult TryReinterpretCast(Sema &Self, Expr *SrcExpr,
                                        QualType DestType, bool CStyle,
                                        const SourceRange &OpRange,
                                        unsigned &msg,
                                        CastKind &Kind);

/// ActOnCXXNamedCast - Parse {dynamic,static,reinterpret,const}_cast's.
ExprResult
Sema::ActOnCXXNamedCast(SourceLocation OpLoc, tok::TokenKind Kind,
                        SourceLocation LAngleBracketLoc, ParsedType Ty,
                        SourceLocation RAngleBracketLoc,
                        SourceLocation LParenLoc, Expr *E,
                        SourceLocation RParenLoc) {
  
  TypeSourceInfo *DestTInfo;
  QualType DestType = GetTypeFromParser(Ty, &DestTInfo);
  if (!DestTInfo)
    DestTInfo = Context.getTrivialTypeSourceInfo(DestType, SourceLocation());

  return BuildCXXNamedCast(OpLoc, Kind, DestTInfo, move(E),
                           SourceRange(LAngleBracketLoc, RAngleBracketLoc),
                           SourceRange(LParenLoc, RParenLoc));
}

ExprResult
Sema::BuildCXXNamedCast(SourceLocation OpLoc, tok::TokenKind Kind,
                        TypeSourceInfo *DestTInfo, Expr *Ex,
                        SourceRange AngleBrackets, SourceRange Parens) {
  QualType DestType = DestTInfo->getType();

  SourceRange OpRange(OpLoc, Parens.getEnd());
  SourceRange DestRange = AngleBrackets;

  // If the type is dependent, we won't do the semantic analysis now.
  // FIXME: should we check this in a more fine-grained manner?
  bool TypeDependent = DestType->isDependentType() || Ex->isTypeDependent();

  switch (Kind) {
  default: assert(0 && "Unknown C++ cast!");

  case tok::kw_const_cast:
    if (!TypeDependent)
      CheckConstCast(*this, Ex, DestType, OpRange, DestRange);
    return Owned(CXXConstCastExpr::Create(Context,
                                        DestType.getNonLValueExprType(Context),
                                          Ex, DestTInfo, OpLoc));

  case tok::kw_dynamic_cast: {
    CastKind Kind = CK_Unknown;
    CXXCastPath BasePath;
    if (!TypeDependent)
      CheckDynamicCast(*this, Ex, DestType, OpRange, DestRange, Kind, BasePath);
    return Owned(CXXDynamicCastExpr::Create(Context,
                                          DestType.getNonLValueExprType(Context),
                                            Kind, Ex, &BasePath, DestTInfo,
                                            OpLoc));
  }
  case tok::kw_reinterpret_cast: {
    CastKind Kind = CK_Unknown;
    if (!TypeDependent)
      CheckReinterpretCast(*this, Ex, DestType, OpRange, DestRange, Kind);
    return Owned(CXXReinterpretCastExpr::Create(Context,
                                  DestType.getNonLValueExprType(Context),
                                  Kind, Ex, 0,
                                  DestTInfo, OpLoc));
  }
  case tok::kw_static_cast: {
    CastKind Kind = CK_Unknown;
    CXXCastPath BasePath;
    if (!TypeDependent)
      CheckStaticCast(*this, Ex, DestType, OpRange, Kind, BasePath);
    
    return Owned(CXXStaticCastExpr::Create(Context,
                                         DestType.getNonLValueExprType(Context),
                                           Kind, Ex, &BasePath,
                                           DestTInfo, OpLoc));
  }
  }

  return ExprError();
}

/// UnwrapDissimilarPointerTypes - Like Sema::UnwrapSimilarPointerTypes,
/// this removes one level of indirection from both types, provided that they're
/// the same kind of pointer (plain or to-member). Unlike the Sema function,
/// this one doesn't care if the two pointers-to-member don't point into the
/// same class. This is because CastsAwayConstness doesn't care.
static bool UnwrapDissimilarPointerTypes(QualType& T1, QualType& T2) {
  const PointerType *T1PtrType = T1->getAs<PointerType>(),
                    *T2PtrType = T2->getAs<PointerType>();
  if (T1PtrType && T2PtrType) {
    T1 = T1PtrType->getPointeeType();
    T2 = T2PtrType->getPointeeType();
    return true;
  }
  const ObjCObjectPointerType *T1ObjCPtrType = 
                                            T1->getAs<ObjCObjectPointerType>(),
                              *T2ObjCPtrType = 
                                            T2->getAs<ObjCObjectPointerType>();
  if (T1ObjCPtrType) {
    if (T2ObjCPtrType) {
      T1 = T1ObjCPtrType->getPointeeType();
      T2 = T2ObjCPtrType->getPointeeType();
      return true;
    }
    else if (T2PtrType) {
      T1 = T1ObjCPtrType->getPointeeType();
      T2 = T2PtrType->getPointeeType();
      return true;
    }
  }
  else if (T2ObjCPtrType) {
    if (T1PtrType) {
      T2 = T2ObjCPtrType->getPointeeType();
      T1 = T1PtrType->getPointeeType();
      return true;
    }
  }
  
  const MemberPointerType *T1MPType = T1->getAs<MemberPointerType>(),
                          *T2MPType = T2->getAs<MemberPointerType>();
  if (T1MPType && T2MPType) {
    T1 = T1MPType->getPointeeType();
    T2 = T2MPType->getPointeeType();
    return true;
  }
  
  const BlockPointerType *T1BPType = T1->getAs<BlockPointerType>(),
                         *T2BPType = T2->getAs<BlockPointerType>();
  if (T1BPType && T2BPType) {
    T1 = T1BPType->getPointeeType();
    T2 = T2BPType->getPointeeType();
    return true;
  }
  
  return false;
}

/// CastsAwayConstness - Check if the pointer conversion from SrcType to
/// DestType casts away constness as defined in C++ 5.2.11p8ff. This is used by
/// the cast checkers.  Both arguments must denote pointer (possibly to member)
/// types.
static bool
CastsAwayConstness(Sema &Self, QualType SrcType, QualType DestType) {
  // Casting away constness is defined in C++ 5.2.11p8 with reference to
  // C++ 4.4. We piggyback on Sema::IsQualificationConversion for this, since
  // the rules are non-trivial. So first we construct Tcv *...cv* as described
  // in C++ 5.2.11p8.
  assert((SrcType->isAnyPointerType() || SrcType->isMemberPointerType() ||
          SrcType->isBlockPointerType()) &&
         "Source type is not pointer or pointer to member.");
  assert((DestType->isAnyPointerType() || DestType->isMemberPointerType() ||
          DestType->isBlockPointerType()) &&
         "Destination type is not pointer or pointer to member.");

  QualType UnwrappedSrcType = Self.Context.getCanonicalType(SrcType), 
           UnwrappedDestType = Self.Context.getCanonicalType(DestType);
  llvm::SmallVector<Qualifiers, 8> cv1, cv2;

  // Find the qualifications.
  while (UnwrapDissimilarPointerTypes(UnwrappedSrcType, UnwrappedDestType)) {
    Qualifiers SrcQuals;
    Self.Context.getUnqualifiedArrayType(UnwrappedSrcType, SrcQuals);
    cv1.push_back(SrcQuals);
    
    Qualifiers DestQuals;
    Self.Context.getUnqualifiedArrayType(UnwrappedDestType, DestQuals);
    cv2.push_back(DestQuals);
  }
  if (cv1.empty())
    return false;

  // Construct void pointers with those qualifiers (in reverse order of
  // unwrapping, of course).
  QualType SrcConstruct = Self.Context.VoidTy;
  QualType DestConstruct = Self.Context.VoidTy;
  ASTContext &Context = Self.Context;
  for (llvm::SmallVector<Qualifiers, 8>::reverse_iterator i1 = cv1.rbegin(),
                                                          i2 = cv2.rbegin();
       i1 != cv1.rend(); ++i1, ++i2) {
    SrcConstruct
      = Context.getPointerType(Context.getQualifiedType(SrcConstruct, *i1));
    DestConstruct
      = Context.getPointerType(Context.getQualifiedType(DestConstruct, *i2));
  }

  // Test if they're compatible.
  return SrcConstruct != DestConstruct &&
    !Self.IsQualificationConversion(SrcConstruct, DestConstruct);
}

/// CheckDynamicCast - Check that a dynamic_cast\<DestType\>(SrcExpr) is valid.
/// Refer to C++ 5.2.7 for details. Dynamic casts are used mostly for runtime-
/// checked downcasts in class hierarchies.
static void
CheckDynamicCast(Sema &Self, Expr *&SrcExpr, QualType DestType,
                 const SourceRange &OpRange,
                 const SourceRange &DestRange, CastKind &Kind,
                 CXXCastPath &BasePath) {
  QualType OrigDestType = DestType, OrigSrcType = SrcExpr->getType();
  DestType = Self.Context.getCanonicalType(DestType);

  // C++ 5.2.7p1: T shall be a pointer or reference to a complete class type,
  //   or "pointer to cv void".

  QualType DestPointee;
  const PointerType *DestPointer = DestType->getAs<PointerType>();
  const ReferenceType *DestReference = DestType->getAs<ReferenceType>();
  if (DestPointer) {
    DestPointee = DestPointer->getPointeeType();
  } else if (DestReference) {
    DestPointee = DestReference->getPointeeType();
  } else {
    Self.Diag(OpRange.getBegin(), diag::err_bad_dynamic_cast_not_ref_or_ptr)
      << OrigDestType << DestRange;
    return;
  }

  const RecordType *DestRecord = DestPointee->getAs<RecordType>();
  if (DestPointee->isVoidType()) {
    assert(DestPointer && "Reference to void is not possible");
  } else if (DestRecord) {
    if (Self.RequireCompleteType(OpRange.getBegin(), DestPointee,
                               Self.PDiag(diag::err_bad_dynamic_cast_incomplete)
                                   << DestRange))
      return;
  } else {
    Self.Diag(OpRange.getBegin(), diag::err_bad_dynamic_cast_not_class)
      << DestPointee.getUnqualifiedType() << DestRange;
    return;
  }

  // C++0x 5.2.7p2: If T is a pointer type, v shall be an rvalue of a pointer to
  //   complete class type, [...]. If T is an lvalue reference type, v shall be
  //   an lvalue of a complete class type, [...]. If T is an rvalue reference
  //   type, v shall be an expression having a complete effective class type,
  //   [...]

  QualType SrcType = Self.Context.getCanonicalType(OrigSrcType);
  QualType SrcPointee;
  if (DestPointer) {
    if (const PointerType *SrcPointer = SrcType->getAs<PointerType>()) {
      SrcPointee = SrcPointer->getPointeeType();
    } else {
      Self.Diag(OpRange.getBegin(), diag::err_bad_dynamic_cast_not_ptr)
        << OrigSrcType << SrcExpr->getSourceRange();
      return;
    }
  } else if (DestReference->isLValueReferenceType()) {
    if (SrcExpr->isLvalue(Self.Context) != Expr::LV_Valid) {
      Self.Diag(OpRange.getBegin(), diag::err_bad_cxx_cast_rvalue)
        << CT_Dynamic << OrigSrcType << OrigDestType << OpRange;
    }
    SrcPointee = SrcType;
  } else {
    SrcPointee = SrcType;
  }

  const RecordType *SrcRecord = SrcPointee->getAs<RecordType>();
  if (SrcRecord) {
    if (Self.RequireCompleteType(OpRange.getBegin(), SrcPointee,
                             Self.PDiag(diag::err_bad_dynamic_cast_incomplete)
                                   << SrcExpr->getSourceRange()))
      return;
  } else {
    Self.Diag(OpRange.getBegin(), diag::err_bad_dynamic_cast_not_class)
      << SrcPointee.getUnqualifiedType() << SrcExpr->getSourceRange();
    return;
  }

  assert((DestPointer || DestReference) &&
    "Bad destination non-ptr/ref slipped through.");
  assert((DestRecord || DestPointee->isVoidType()) &&
    "Bad destination pointee slipped through.");
  assert(SrcRecord && "Bad source pointee slipped through.");

  // C++ 5.2.7p1: The dynamic_cast operator shall not cast away constness.
  if (!DestPointee.isAtLeastAsQualifiedAs(SrcPointee)) {
    Self.Diag(OpRange.getBegin(), diag::err_bad_cxx_cast_const_away)
      << CT_Dynamic << OrigSrcType << OrigDestType << OpRange;
    return;
  }

  // C++ 5.2.7p3: If the type of v is the same as the required result type,
  //   [except for cv].
  if (DestRecord == SrcRecord) {
    Kind = CK_NoOp;
    return;
  }

  // C++ 5.2.7p5
  // Upcasts are resolved statically.
  if (DestRecord && Self.IsDerivedFrom(SrcPointee, DestPointee)) {
    if (Self.CheckDerivedToBaseConversion(SrcPointee, DestPointee,
                                           OpRange.getBegin(), OpRange, 
                                           &BasePath))
        return;
        
    Kind = CK_DerivedToBase;

    // If we are casting to or through a virtual base class, we need a
    // vtable.
    if (Self.BasePathInvolvesVirtualBase(BasePath))
      Self.MarkVTableUsed(OpRange.getBegin(), 
                          cast<CXXRecordDecl>(SrcRecord->getDecl()));
    return;
  }

  // C++ 5.2.7p6: Otherwise, v shall be [polymorphic].
  const RecordDecl *SrcDecl = SrcRecord->getDecl()->getDefinition();
  assert(SrcDecl && "Definition missing");
  if (!cast<CXXRecordDecl>(SrcDecl)->isPolymorphic()) {
    Self.Diag(OpRange.getBegin(), diag::err_bad_dynamic_cast_not_polymorphic)
      << SrcPointee.getUnqualifiedType() << SrcExpr->getSourceRange();
  }
  Self.MarkVTableUsed(OpRange.getBegin(), 
                      cast<CXXRecordDecl>(SrcRecord->getDecl()));

  // Done. Everything else is run-time checks.
  Kind = CK_Dynamic;
}

/// CheckConstCast - Check that a const_cast\<DestType\>(SrcExpr) is valid.
/// Refer to C++ 5.2.11 for details. const_cast is typically used in code
/// like this:
/// const char *str = "literal";
/// legacy_function(const_cast\<char*\>(str));
void
CheckConstCast(Sema &Self, Expr *&SrcExpr, QualType DestType,
               const SourceRange &OpRange, const SourceRange &DestRange) {
  if (!DestType->isLValueReferenceType())
    Self.DefaultFunctionArrayLvalueConversion(SrcExpr);

  unsigned msg = diag::err_bad_cxx_cast_generic;
  if (TryConstCast(Self, SrcExpr, DestType, /*CStyle*/false, msg) != TC_Success
      && msg != 0)
    Self.Diag(OpRange.getBegin(), msg) << CT_Const
      << SrcExpr->getType() << DestType << OpRange;
}

/// CheckReinterpretCast - Check that a reinterpret_cast\<DestType\>(SrcExpr) is
/// valid.
/// Refer to C++ 5.2.10 for details. reinterpret_cast is typically used in code
/// like this:
/// char *bytes = reinterpret_cast\<char*\>(int_ptr);
void
CheckReinterpretCast(Sema &Self, Expr *&SrcExpr, QualType DestType,
                     const SourceRange &OpRange, const SourceRange &DestRange,
                     CastKind &Kind) {
  if (!DestType->isLValueReferenceType())
    Self.DefaultFunctionArrayLvalueConversion(SrcExpr);

  unsigned msg = diag::err_bad_cxx_cast_generic;
  if (TryReinterpretCast(Self, SrcExpr, DestType, /*CStyle*/false, OpRange,
                         msg, Kind)
      != TC_Success && msg != 0)
    Self.Diag(OpRange.getBegin(), msg) << CT_Reinterpret
      << SrcExpr->getType() << DestType << OpRange;
}


/// CheckStaticCast - Check that a static_cast\<DestType\>(SrcExpr) is valid.
/// Refer to C++ 5.2.9 for details. Static casts are mostly used for making
/// implicit conversions explicit and getting rid of data loss warnings.
void
CheckStaticCast(Sema &Self, Expr *&SrcExpr, QualType DestType,
                const SourceRange &OpRange, CastKind &Kind,
                CXXCastPath &BasePath) {
  // This test is outside everything else because it's the only case where
  // a non-lvalue-reference target type does not lead to decay.
  // C++ 5.2.9p4: Any expression can be explicitly converted to type "cv void".
  if (DestType->isVoidType()) {
    Kind = CK_ToVoid;
    return;
  }

  if (!DestType->isLValueReferenceType() && !DestType->isRecordType())
    Self.DefaultFunctionArrayLvalueConversion(SrcExpr);

  unsigned msg = diag::err_bad_cxx_cast_generic;
  if (TryStaticCast(Self, SrcExpr, DestType, /*CStyle*/false, OpRange, msg,
                    Kind, BasePath) != TC_Success && msg != 0)
    Self.Diag(OpRange.getBegin(), msg) << CT_Static
      << SrcExpr->getType() << DestType << OpRange;
  else if (Kind == CK_Unknown || Kind == CK_BitCast)
    Self.CheckCastAlign(SrcExpr, DestType, OpRange);
}

/// TryStaticCast - Check if a static cast can be performed, and do so if
/// possible. If @p CStyle, ignore access restrictions on hierarchy casting
/// and casting away constness.
static TryCastResult TryStaticCast(Sema &Self, Expr *&SrcExpr,
                                   QualType DestType, bool CStyle,
                                   const SourceRange &OpRange, unsigned &msg,
                                   CastKind &Kind,
                                   CXXCastPath &BasePath) {
  // The order the tests is not entirely arbitrary. There is one conversion
  // that can be handled in two different ways. Given:
  // struct A {};
  // struct B : public A {
  //   B(); B(const A&);
  // };
  // const A &a = B();
  // the cast static_cast<const B&>(a) could be seen as either a static
  // reference downcast, or an explicit invocation of the user-defined
  // conversion using B's conversion constructor.
  // DR 427 specifies that the downcast is to be applied here.

  // C++ 5.2.9p4: Any expression can be explicitly converted to type "cv void".
  // Done outside this function.

  TryCastResult tcr;

  // C++ 5.2.9p5, reference downcast.
  // See the function for details.
  // DR 427 specifies that this is to be applied before paragraph 2.
  tcr = TryStaticReferenceDowncast(Self, SrcExpr, DestType, CStyle, OpRange,
                                   msg, Kind, BasePath);
  if (tcr != TC_NotApplicable)
    return tcr;

  // N2844 5.2.9p3: An lvalue of type "cv1 T1" can be cast to type "rvalue
  //   reference to cv2 T2" if "cv2 T2" is reference-compatible with "cv1 T1".
  tcr = TryLValueToRValueCast(Self, SrcExpr, DestType, msg);
  if (tcr != TC_NotApplicable) {
    Kind = CK_NoOp;
    return tcr;
  }

  // C++ 5.2.9p2: An expression e can be explicitly converted to a type T
  //   [...] if the declaration "T t(e);" is well-formed, [...].
  tcr = TryStaticImplicitCast(Self, SrcExpr, DestType, CStyle, OpRange, msg,
                              Kind);
  if (tcr != TC_NotApplicable)
    return tcr;
  
  // C++ 5.2.9p6: May apply the reverse of any standard conversion, except
  // lvalue-to-rvalue, array-to-pointer, function-to-pointer, and boolean
  // conversions, subject to further restrictions.
  // Also, C++ 5.2.9p1 forbids casting away constness, which makes reversal
  // of qualification conversions impossible.
  // In the CStyle case, the earlier attempt to const_cast should have taken
  // care of reverse qualification conversions.

  QualType OrigSrcType = SrcExpr->getType();

  QualType SrcType = Self.Context.getCanonicalType(SrcExpr->getType());

  // Reverse integral promotion/conversion. All such conversions are themselves
  // again integral promotions or conversions and are thus already handled by
  // p2 (TryDirectInitialization above).
  // (Note: any data loss warnings should be suppressed.)
  // The exception is the reverse of enum->integer, i.e. integer->enum (and
  // enum->enum). See also C++ 5.2.9p7.
  // The same goes for reverse floating point promotion/conversion and
  // floating-integral conversions. Again, only floating->enum is relevant.
  if (DestType->isEnumeralType()) {
    if (SrcType->isComplexType() || SrcType->isVectorType()) {
      // Fall through - these cannot be converted.
    } else if (SrcType->isArithmeticType() || SrcType->isEnumeralType()) {
      Kind = CK_IntegralCast;
      return TC_Success;
    }
  }

  // Reverse pointer upcast. C++ 4.10p3 specifies pointer upcast.
  // C++ 5.2.9p8 additionally disallows a cast path through virtual inheritance.
  tcr = TryStaticPointerDowncast(Self, SrcType, DestType, CStyle, OpRange, msg,
                                 Kind, BasePath);
  if (tcr != TC_NotApplicable)
    return tcr;

  // Reverse member pointer conversion. C++ 4.11 specifies member pointer
  // conversion. C++ 5.2.9p9 has additional information.
  // DR54's access restrictions apply here also.
  tcr = TryStaticMemberPointerUpcast(Self, SrcExpr, SrcType, DestType, CStyle,
                                     OpRange, msg, Kind, BasePath);
  if (tcr != TC_NotApplicable)
    return tcr;

  // Reverse pointer conversion to void*. C++ 4.10.p2 specifies conversion to
  // void*. C++ 5.2.9p10 specifies additional restrictions, which really is
  // just the usual constness stuff.
  if (const PointerType *SrcPointer = SrcType->getAs<PointerType>()) {
    QualType SrcPointee = SrcPointer->getPointeeType();
    if (SrcPointee->isVoidType()) {
      if (const PointerType *DestPointer = DestType->getAs<PointerType>()) {
        QualType DestPointee = DestPointer->getPointeeType();
        if (DestPointee->isIncompleteOrObjectType()) {
          // This is definitely the intended conversion, but it might fail due
          // to a const violation.
          if (!CStyle && !DestPointee.isAtLeastAsQualifiedAs(SrcPointee)) {
            msg = diag::err_bad_cxx_cast_const_away;
            return TC_Failed;
          }
          Kind = CK_BitCast;
          return TC_Success;
        }
      }
      else if (DestType->isObjCObjectPointerType()) {
        // allow both c-style cast and static_cast of objective-c pointers as 
        // they are pervasive.
        Kind = CK_AnyPointerToObjCPointerCast;
        return TC_Success;
      }
      else if (CStyle && DestType->isBlockPointerType()) {
        // allow c-style cast of void * to block pointers.
        Kind = CK_AnyPointerToBlockPointerCast;
        return TC_Success;
      }
    }
  }
  // Allow arbitray objective-c pointer conversion with static casts.
  if (SrcType->isObjCObjectPointerType() &&
      DestType->isObjCObjectPointerType())
    return TC_Success;
  
  // We tried everything. Everything! Nothing works! :-(
  return TC_NotApplicable;
}

/// Tests whether a conversion according to N2844 is valid.
TryCastResult
TryLValueToRValueCast(Sema &Self, Expr *SrcExpr, QualType DestType,
                      unsigned &msg) {
  // N2844 5.2.9p3: An lvalue of type "cv1 T1" can be cast to type "rvalue
  //   reference to cv2 T2" if "cv2 T2" is reference-compatible with "cv1 T1".
  const RValueReferenceType *R = DestType->getAs<RValueReferenceType>();
  if (!R)
    return TC_NotApplicable;

  if (SrcExpr->isLvalue(Self.Context) != Expr::LV_Valid)
    return TC_NotApplicable;

  // Because we try the reference downcast before this function, from now on
  // this is the only cast possibility, so we issue an error if we fail now.
  // FIXME: Should allow casting away constness if CStyle.
  bool DerivedToBase;
  bool ObjCConversion;
  if (Self.CompareReferenceRelationship(SrcExpr->getLocStart(),
                                        SrcExpr->getType(), R->getPointeeType(),
                                        DerivedToBase, ObjCConversion) <
        Sema::Ref_Compatible_With_Added_Qualification) {
    msg = diag::err_bad_lvalue_to_rvalue_cast;
    return TC_Failed;
  }

  // FIXME: We should probably have an AST node for lvalue-to-rvalue 
  // conversions.
  return TC_Success;
}

/// Tests whether a conversion according to C++ 5.2.9p5 is valid.
TryCastResult
TryStaticReferenceDowncast(Sema &Self, Expr *SrcExpr, QualType DestType,
                           bool CStyle, const SourceRange &OpRange,
                           unsigned &msg, CastKind &Kind,
                           CXXCastPath &BasePath) {
  // C++ 5.2.9p5: An lvalue of type "cv1 B", where B is a class type, can be
  //   cast to type "reference to cv2 D", where D is a class derived from B,
  //   if a valid standard conversion from "pointer to D" to "pointer to B"
  //   exists, cv2 >= cv1, and B is not a virtual base class of D.
  // In addition, DR54 clarifies that the base must be accessible in the
  // current context. Although the wording of DR54 only applies to the pointer
  // variant of this rule, the intent is clearly for it to apply to the this
  // conversion as well.

  const ReferenceType *DestReference = DestType->getAs<ReferenceType>();
  if (!DestReference) {
    return TC_NotApplicable;
  }
  bool RValueRef = DestReference->isRValueReferenceType();
  if (!RValueRef && SrcExpr->isLvalue(Self.Context) != Expr::LV_Valid) {
    // We know the left side is an lvalue reference, so we can suggest a reason.
    msg = diag::err_bad_cxx_cast_rvalue;
    return TC_NotApplicable;
  }

  QualType DestPointee = DestReference->getPointeeType();

  return TryStaticDowncast(Self, 
                           Self.Context.getCanonicalType(SrcExpr->getType()), 
                           Self.Context.getCanonicalType(DestPointee), CStyle,
                           OpRange, SrcExpr->getType(), DestType, msg, Kind,
                           BasePath);
}

/// Tests whether a conversion according to C++ 5.2.9p8 is valid.
TryCastResult
TryStaticPointerDowncast(Sema &Self, QualType SrcType, QualType DestType,
                         bool CStyle, const SourceRange &OpRange,
                         unsigned &msg, CastKind &Kind,
                         CXXCastPath &BasePath) {
  // C++ 5.2.9p8: An rvalue of type "pointer to cv1 B", where B is a class
  //   type, can be converted to an rvalue of type "pointer to cv2 D", where D
  //   is a class derived from B, if a valid standard conversion from "pointer
  //   to D" to "pointer to B" exists, cv2 >= cv1, and B is not a virtual base
  //   class of D.
  // In addition, DR54 clarifies that the base must be accessible in the
  // current context.

  const PointerType *DestPointer = DestType->getAs<PointerType>();
  if (!DestPointer) {
    return TC_NotApplicable;
  }

  const PointerType *SrcPointer = SrcType->getAs<PointerType>();
  if (!SrcPointer) {
    msg = diag::err_bad_static_cast_pointer_nonpointer;
    return TC_NotApplicable;
  }

  return TryStaticDowncast(Self, 
                   Self.Context.getCanonicalType(SrcPointer->getPointeeType()),
                  Self.Context.getCanonicalType(DestPointer->getPointeeType()), 
                           CStyle, OpRange, SrcType, DestType, msg, Kind,
                           BasePath);
}

/// TryStaticDowncast - Common functionality of TryStaticReferenceDowncast and
/// TryStaticPointerDowncast. Tests whether a static downcast from SrcType to
/// DestType is possible and allowed.
TryCastResult
TryStaticDowncast(Sema &Self, CanQualType SrcType, CanQualType DestType,
                  bool CStyle, const SourceRange &OpRange, QualType OrigSrcType,
                  QualType OrigDestType, unsigned &msg, 
                  CastKind &Kind, CXXCastPath &BasePath) {
  // We can only work with complete types. But don't complain if it doesn't work
  if (Self.RequireCompleteType(OpRange.getBegin(), SrcType, Self.PDiag(0)) ||
      Self.RequireCompleteType(OpRange.getBegin(), DestType, Self.PDiag(0)))
    return TC_NotApplicable;

  // Downcast can only happen in class hierarchies, so we need classes.
  if (!DestType->getAs<RecordType>() || !SrcType->getAs<RecordType>()) {
    return TC_NotApplicable;
  }

  CXXBasePaths Paths(/*FindAmbiguities=*/true, /*RecordPaths=*/true,
                     /*DetectVirtual=*/true);
  if (!Self.IsDerivedFrom(DestType, SrcType, Paths)) {
    return TC_NotApplicable;
  }

  // Target type does derive from source type. Now we're serious. If an error
  // appears now, it's not ignored.
  // This may not be entirely in line with the standard. Take for example:
  // struct A {};
  // struct B : virtual A {
  //   B(A&);
  // };
  //
  // void f()
  // {
  //   (void)static_cast<const B&>(*((A*)0));
  // }
  // As far as the standard is concerned, p5 does not apply (A is virtual), so
  // p2 should be used instead - "const B& t(*((A*)0));" is perfectly valid.
  // However, both GCC and Comeau reject this example, and accepting it would
  // mean more complex code if we're to preserve the nice error message.
  // FIXME: Being 100% compliant here would be nice to have.

  // Must preserve cv, as always, unless we're in C-style mode.
  if (!CStyle && !DestType.isAtLeastAsQualifiedAs(SrcType)) {
    msg = diag::err_bad_cxx_cast_const_away;
    return TC_Failed;
  }

  if (Paths.isAmbiguous(SrcType.getUnqualifiedType())) {
    // This code is analoguous to that in CheckDerivedToBaseConversion, except
    // that it builds the paths in reverse order.
    // To sum up: record all paths to the base and build a nice string from
    // them. Use it to spice up the error message.
    if (!Paths.isRecordingPaths()) {
      Paths.clear();
      Paths.setRecordingPaths(true);
      Self.IsDerivedFrom(DestType, SrcType, Paths);
    }
    std::string PathDisplayStr;
    std::set<unsigned> DisplayedPaths;
    for (CXXBasePaths::paths_iterator PI = Paths.begin(), PE = Paths.end();
         PI != PE; ++PI) {
      if (DisplayedPaths.insert(PI->back().SubobjectNumber).second) {
        // We haven't displayed a path to this particular base
        // class subobject yet.
        PathDisplayStr += "\n    ";
        for (CXXBasePath::const_reverse_iterator EI = PI->rbegin(),
                                                 EE = PI->rend();
             EI != EE; ++EI)
          PathDisplayStr += EI->Base->getType().getAsString() + " -> ";
        PathDisplayStr += QualType(DestType).getAsString();
      }
    }

    Self.Diag(OpRange.getBegin(), diag::err_ambiguous_base_to_derived_cast)
      << QualType(SrcType).getUnqualifiedType() 
      << QualType(DestType).getUnqualifiedType()
      << PathDisplayStr << OpRange;
    msg = 0;
    return TC_Failed;
  }

  if (Paths.getDetectedVirtual() != 0) {
    QualType VirtualBase(Paths.getDetectedVirtual(), 0);
    Self.Diag(OpRange.getBegin(), diag::err_static_downcast_via_virtual)
      << OrigSrcType << OrigDestType << VirtualBase << OpRange;
    msg = 0;
    return TC_Failed;
  }

  if (!CStyle && Self.CheckBaseClassAccess(OpRange.getBegin(),
                                           SrcType, DestType,
                                           Paths.front(),
                                diag::err_downcast_from_inaccessible_base)) {
    msg = 0;
    return TC_Failed;
  }

  Self.BuildBasePathArray(Paths, BasePath);
  Kind = CK_BaseToDerived;
  return TC_Success;
}

/// TryStaticMemberPointerUpcast - Tests whether a conversion according to
/// C++ 5.2.9p9 is valid:
///
///   An rvalue of type "pointer to member of D of type cv1 T" can be
///   converted to an rvalue of type "pointer to member of B of type cv2 T",
///   where B is a base class of D [...].
///
TryCastResult
TryStaticMemberPointerUpcast(Sema &Self, Expr *&SrcExpr, QualType SrcType, 
                             QualType DestType, bool CStyle, 
                             const SourceRange &OpRange,
                             unsigned &msg, CastKind &Kind,
                             CXXCastPath &BasePath) {
  const MemberPointerType *DestMemPtr = DestType->getAs<MemberPointerType>();
  if (!DestMemPtr)
    return TC_NotApplicable;

  bool WasOverloadedFunction = false;
  DeclAccessPair FoundOverload;
  if (SrcExpr->getType() == Self.Context.OverloadTy) {
    if (FunctionDecl *Fn
          = Self.ResolveAddressOfOverloadedFunction(SrcExpr, DestType, false,
                                                    FoundOverload)) {
      CXXMethodDecl *M = cast<CXXMethodDecl>(Fn);
      SrcType = Self.Context.getMemberPointerType(Fn->getType(),
                      Self.Context.getTypeDeclType(M->getParent()).getTypePtr());
      WasOverloadedFunction = true;
    }
  }
  
  const MemberPointerType *SrcMemPtr = SrcType->getAs<MemberPointerType>();
  if (!SrcMemPtr) {
    msg = diag::err_bad_static_cast_member_pointer_nonmp;
    return TC_NotApplicable;
  }

  // T == T, modulo cv
  if (!Self.Context.hasSameUnqualifiedType(SrcMemPtr->getPointeeType(),
                                           DestMemPtr->getPointeeType()))
    return TC_NotApplicable;

  // B base of D
  QualType SrcClass(SrcMemPtr->getClass(), 0);
  QualType DestClass(DestMemPtr->getClass(), 0);
  CXXBasePaths Paths(/*FindAmbiguities=*/true, /*RecordPaths=*/true,
                  /*DetectVirtual=*/true);
  if (!Self.IsDerivedFrom(SrcClass, DestClass, Paths)) {
    return TC_NotApplicable;
  }

  // B is a base of D. But is it an allowed base? If not, it's a hard error.
  if (Paths.isAmbiguous(Self.Context.getCanonicalType(DestClass))) {
    Paths.clear();
    Paths.setRecordingPaths(true);
    bool StillOkay = Self.IsDerivedFrom(SrcClass, DestClass, Paths);
    assert(StillOkay);
    StillOkay = StillOkay;
    std::string PathDisplayStr = Self.getAmbiguousPathsDisplayString(Paths);
    Self.Diag(OpRange.getBegin(), diag::err_ambiguous_memptr_conv)
      << 1 << SrcClass << DestClass << PathDisplayStr << OpRange;
    msg = 0;
    return TC_Failed;
  }

  if (const RecordType *VBase = Paths.getDetectedVirtual()) {
    Self.Diag(OpRange.getBegin(), diag::err_memptr_conv_via_virtual)
      << SrcClass << DestClass << QualType(VBase, 0) << OpRange;
    msg = 0;
    return TC_Failed;
  }

  if (!CStyle && Self.CheckBaseClassAccess(OpRange.getBegin(),
                                           DestClass, SrcClass,
                                           Paths.front(),
                                     diag::err_upcast_to_inaccessible_base)) {
    msg = 0;
    return TC_Failed;
  }

  if (WasOverloadedFunction) {
    // Resolve the address of the overloaded function again, this time
    // allowing complaints if something goes wrong.
    FunctionDecl *Fn = Self.ResolveAddressOfOverloadedFunction(SrcExpr, 
                                                               DestType, 
                                                               true,
                                                               FoundOverload);
    if (!Fn) {
      msg = 0;
      return TC_Failed;
    }

    SrcExpr = Self.FixOverloadedFunctionReference(SrcExpr, FoundOverload, Fn);
    if (!SrcExpr) {
      msg = 0;
      return TC_Failed;
    }
  }

  Self.BuildBasePathArray(Paths, BasePath);
  Kind = CK_DerivedToBaseMemberPointer;
  return TC_Success;
}

/// TryStaticImplicitCast - Tests whether a conversion according to C++ 5.2.9p2
/// is valid:
///
///   An expression e can be explicitly converted to a type T using a
///   @c static_cast if the declaration "T t(e);" is well-formed [...].
TryCastResult
TryStaticImplicitCast(Sema &Self, Expr *&SrcExpr, QualType DestType,
                      bool CStyle, const SourceRange &OpRange, unsigned &msg,
                      CastKind &Kind) {
  if (DestType->isRecordType()) {
    if (Self.RequireCompleteType(OpRange.getBegin(), DestType,
                                 diag::err_bad_dynamic_cast_incomplete)) {
      msg = 0;
      return TC_Failed;
    }
  }
  
  // At this point of CheckStaticCast, if the destination is a reference,
  // this has to work. There is no other way that works.
  // On the other hand, if we're checking a C-style cast, we've still got
  // the reinterpret_cast way.
  InitializedEntity Entity = InitializedEntity::InitializeTemporary(DestType);
  InitializationKind InitKind
    = InitializationKind::CreateCast(/*FIXME:*/OpRange, 
                                                               CStyle);    
  InitializationSequence InitSeq(Self, Entity, InitKind, &SrcExpr, 1);
  if (InitSeq.getKind() == InitializationSequence::FailedSequence && 
      (CStyle || !DestType->isReferenceType()))
    return TC_NotApplicable;
    
  ExprResult Result
    = InitSeq.Perform(Self, Entity, InitKind, MultiExprArg(Self, &SrcExpr, 1));
  if (Result.isInvalid()) {
    msg = 0;
    return TC_Failed;
  }
  
  if (InitSeq.isConstructorInitialization())
    Kind = CK_ConstructorConversion;
  else
    Kind = CK_NoOp;
  
  SrcExpr = Result.takeAs<Expr>();
  return TC_Success;
}

/// TryConstCast - See if a const_cast from source to destination is allowed,
/// and perform it if it is.
static TryCastResult TryConstCast(Sema &Self, Expr *SrcExpr, QualType DestType,
                                  bool CStyle, unsigned &msg) {
  DestType = Self.Context.getCanonicalType(DestType);
  QualType SrcType = SrcExpr->getType();
  if (const LValueReferenceType *DestTypeTmp =
        DestType->getAs<LValueReferenceType>()) {
    if (SrcExpr->isLvalue(Self.Context) != Expr::LV_Valid) {
      // Cannot const_cast non-lvalue to lvalue reference type. But if this
      // is C-style, static_cast might find a way, so we simply suggest a
      // message and tell the parent to keep searching.
      msg = diag::err_bad_cxx_cast_rvalue;
      return TC_NotApplicable;
    }

    // C++ 5.2.11p4: An lvalue of type T1 can be [cast] to an lvalue of type T2
    //   [...] if a pointer to T1 can be [cast] to the type pointer to T2.
    DestType = Self.Context.getPointerType(DestTypeTmp->getPointeeType());
    SrcType = Self.Context.getPointerType(SrcType);
  }

  // C++ 5.2.11p5: For a const_cast involving pointers to data members [...]
  //   the rules for const_cast are the same as those used for pointers.

  if (!DestType->isPointerType() &&
      !DestType->isMemberPointerType() &&
      !DestType->isObjCObjectPointerType()) {
    // Cannot cast to non-pointer, non-reference type. Note that, if DestType
    // was a reference type, we converted it to a pointer above.
    // The status of rvalue references isn't entirely clear, but it looks like
    // conversion to them is simply invalid.
    // C++ 5.2.11p3: For two pointer types [...]
    if (!CStyle)
      msg = diag::err_bad_const_cast_dest;
    return TC_NotApplicable;
  }
  if (DestType->isFunctionPointerType() ||
      DestType->isMemberFunctionPointerType()) {
    // Cannot cast direct function pointers.
    // C++ 5.2.11p2: [...] where T is any object type or the void type [...]
    // T is the ultimate pointee of source and target type.
    if (!CStyle)
      msg = diag::err_bad_const_cast_dest;
    return TC_NotApplicable;
  }
  SrcType = Self.Context.getCanonicalType(SrcType);

  // Unwrap the pointers. Ignore qualifiers. Terminate early if the types are
  // completely equal.
  // FIXME: const_cast should probably not be able to convert between pointers
  // to different address spaces.
  // C++ 5.2.11p3 describes the core semantics of const_cast. All cv specifiers
  // in multi-level pointers may change, but the level count must be the same,
  // as must be the final pointee type.
  while (SrcType != DestType &&
         Self.Context.UnwrapSimilarPointerTypes(SrcType, DestType)) {
    Qualifiers Quals;
    SrcType = Self.Context.getUnqualifiedArrayType(SrcType, Quals);
    DestType = Self.Context.getUnqualifiedArrayType(DestType, Quals);
  }

  // Since we're dealing in canonical types, the remainder must be the same.
  if (SrcType != DestType)
    return TC_NotApplicable;

  return TC_Success;
}

static TryCastResult TryReinterpretCast(Sema &Self, Expr *SrcExpr,
                                        QualType DestType, bool CStyle,
                                        const SourceRange &OpRange,
                                        unsigned &msg,
                                        CastKind &Kind) {
  bool IsLValueCast = false;
  
  DestType = Self.Context.getCanonicalType(DestType);
  QualType SrcType = SrcExpr->getType();
  if (const ReferenceType *DestTypeTmp = DestType->getAs<ReferenceType>()) {
    bool LValue = DestTypeTmp->isLValueReferenceType();
    if (LValue && SrcExpr->isLvalue(Self.Context) != Expr::LV_Valid) {
      // Cannot cast non-lvalue to reference type. See the similar comment in
      // const_cast.
      msg = diag::err_bad_cxx_cast_rvalue;
      return TC_NotApplicable;
    }

    // C++ 5.2.10p10: [...] a reference cast reinterpret_cast<T&>(x) has the
    //   same effect as the conversion *reinterpret_cast<T*>(&x) with the
    //   built-in & and * operators.
    // This code does this transformation for the checked types.
    DestType = Self.Context.getPointerType(DestTypeTmp->getPointeeType());
    SrcType = Self.Context.getPointerType(SrcType);
    IsLValueCast = true;
  }

  // Canonicalize source for comparison.
  SrcType = Self.Context.getCanonicalType(SrcType);

  const MemberPointerType *DestMemPtr = DestType->getAs<MemberPointerType>(),
                          *SrcMemPtr = SrcType->getAs<MemberPointerType>();
  if (DestMemPtr && SrcMemPtr) {
    // C++ 5.2.10p9: An rvalue of type "pointer to member of X of type T1"
    //   can be explicitly converted to an rvalue of type "pointer to member
    //   of Y of type T2" if T1 and T2 are both function types or both object
    //   types.
    if (DestMemPtr->getPointeeType()->isFunctionType() !=
        SrcMemPtr->getPointeeType()->isFunctionType())
      return TC_NotApplicable;

    // C++ 5.2.10p2: The reinterpret_cast operator shall not cast away
    //   constness.
    // A reinterpret_cast followed by a const_cast can, though, so in C-style,
    // we accept it.
    if (!CStyle && CastsAwayConstness(Self, SrcType, DestType)) {
      msg = diag::err_bad_cxx_cast_const_away;
      return TC_Failed;
    }

    // Don't allow casting between member pointers of different sizes.
    if (Self.Context.getTypeSize(DestMemPtr) !=
        Self.Context.getTypeSize(SrcMemPtr)) {
      msg = diag::err_bad_cxx_cast_member_pointer_size;
      return TC_Failed;
    }

    // A valid member pointer cast.
    Kind = IsLValueCast? CK_LValueBitCast : CK_BitCast;
    return TC_Success;
  }

  // See below for the enumeral issue.
  if (SrcType->isNullPtrType() && DestType->isIntegralType(Self.Context)) {
    // C++0x 5.2.10p4: A pointer can be explicitly converted to any integral
    //   type large enough to hold it. A value of std::nullptr_t can be
    //   converted to an integral type; the conversion has the same meaning
    //   and validity as a conversion of (void*)0 to the integral type.
    if (Self.Context.getTypeSize(SrcType) >
        Self.Context.getTypeSize(DestType)) {
      msg = diag::err_bad_reinterpret_cast_small_int;
      return TC_Failed;
    }
    Kind = CK_PointerToIntegral;
    return TC_Success;
  }

  bool destIsVector = DestType->isVectorType();
  bool srcIsVector = SrcType->isVectorType();
  if (srcIsVector || destIsVector) {
    // FIXME: Should this also apply to floating point types?
    bool srcIsScalar = SrcType->isIntegralType(Self.Context);
    bool destIsScalar = DestType->isIntegralType(Self.Context);
    
    // Check if this is a cast between a vector and something else.
    if (!(srcIsScalar && destIsVector) && !(srcIsVector && destIsScalar) &&
        !(srcIsVector && destIsVector))
      return TC_NotApplicable;

    // If both types have the same size, we can successfully cast.
    if (Self.Context.getTypeSize(SrcType)
          == Self.Context.getTypeSize(DestType)) {
      Kind = CK_BitCast;
      return TC_Success;
    }
    
    if (destIsScalar)
      msg = diag::err_bad_cxx_cast_vector_to_scalar_different_size;
    else if (srcIsScalar)
      msg = diag::err_bad_cxx_cast_scalar_to_vector_different_size;
    else
      msg = diag::err_bad_cxx_cast_vector_to_vector_different_size;
    
    return TC_Failed;
  }
  
  bool destIsPtr = DestType->isAnyPointerType() ||
                   DestType->isBlockPointerType();
  bool srcIsPtr = SrcType->isAnyPointerType() ||
                  SrcType->isBlockPointerType();
  if (!destIsPtr && !srcIsPtr) {
    // Except for std::nullptr_t->integer and lvalue->reference, which are
    // handled above, at least one of the two arguments must be a pointer.
    return TC_NotApplicable;
  }

  if (SrcType == DestType) {
    // C++ 5.2.10p2 has a note that mentions that, subject to all other
    // restrictions, a cast to the same type is allowed. The intent is not
    // entirely clear here, since all other paragraphs explicitly forbid casts
    // to the same type. However, the behavior of compilers is pretty consistent
    // on this point: allow same-type conversion if the involved types are
    // pointers, disallow otherwise.
    Kind = CK_NoOp;
    return TC_Success;
  }

  if (DestType->isIntegralType(Self.Context)) {
    assert(srcIsPtr && "One type must be a pointer");
    // C++ 5.2.10p4: A pointer can be explicitly converted to any integral
    //   type large enough to hold it.
    if (Self.Context.getTypeSize(SrcType) >
        Self.Context.getTypeSize(DestType)) {
      msg = diag::err_bad_reinterpret_cast_small_int;
      return TC_Failed;
    }
    Kind = CK_PointerToIntegral;
    return TC_Success;
  }

  if (SrcType->isIntegralOrEnumerationType()) {
    assert(destIsPtr && "One type must be a pointer");
    // C++ 5.2.10p5: A value of integral or enumeration type can be explicitly
    //   converted to a pointer.
    Kind = CK_IntegralToPointer;
    return TC_Success;
  }

  if (!destIsPtr || !srcIsPtr) {
    // With the valid non-pointer conversions out of the way, we can be even
    // more stringent.
    return TC_NotApplicable;
  }

  // C++ 5.2.10p2: The reinterpret_cast operator shall not cast away constness.
  // The C-style cast operator can.
  if (!CStyle && CastsAwayConstness(Self, SrcType, DestType)) {
    msg = diag::err_bad_cxx_cast_const_away;
    return TC_Failed;
  }
  
  // Cannot convert between block pointers and Objective-C object pointers.
  if ((SrcType->isBlockPointerType() && DestType->isObjCObjectPointerType()) ||
      (DestType->isBlockPointerType() && SrcType->isObjCObjectPointerType()))
    return TC_NotApplicable;

  // Any pointer can be cast to an Objective-C pointer type with a C-style
  // cast.
  if (CStyle && DestType->isObjCObjectPointerType()) {
    Kind = CK_AnyPointerToObjCPointerCast;
    return TC_Success;
  }
    
  // Not casting away constness, so the only remaining check is for compatible
  // pointer categories.
  Kind = IsLValueCast? CK_LValueBitCast : CK_BitCast;

  if (SrcType->isFunctionPointerType()) {
    if (DestType->isFunctionPointerType()) {
      // C++ 5.2.10p6: A pointer to a function can be explicitly converted to
      // a pointer to a function of a different type.
      return TC_Success;
    }

    // C++0x 5.2.10p8: Converting a pointer to a function into a pointer to
    //   an object type or vice versa is conditionally-supported.
    // Compilers support it in C++03 too, though, because it's necessary for
    // casting the return value of dlsym() and GetProcAddress().
    // FIXME: Conditionally-supported behavior should be configurable in the
    // TargetInfo or similar.
    if (!Self.getLangOptions().CPlusPlus0x)
      Self.Diag(OpRange.getBegin(), diag::ext_cast_fn_obj) << OpRange;
    return TC_Success;
  }

  if (DestType->isFunctionPointerType()) {
    // See above.
    if (!Self.getLangOptions().CPlusPlus0x)
      Self.Diag(OpRange.getBegin(), diag::ext_cast_fn_obj) << OpRange;
    return TC_Success;
  }
  
  // C++ 5.2.10p7: A pointer to an object can be explicitly converted to
  //   a pointer to an object of different type.
  // Void pointers are not specified, but supported by every compiler out there.
  // So we finish by allowing everything that remains - it's got to be two
  // object pointers.
  return TC_Success;
}

bool 
Sema::CXXCheckCStyleCast(SourceRange R, QualType CastTy, Expr *&CastExpr,
                         CastKind &Kind, 
                         CXXCastPath &BasePath,
                         bool FunctionalStyle) {
  // This test is outside everything else because it's the only case where
  // a non-lvalue-reference target type does not lead to decay.
  // C++ 5.2.9p4: Any expression can be explicitly converted to type "cv void".
  if (CastTy->isVoidType()) {
    Kind = CK_ToVoid;
    return false;
  }

  // If the type is dependent, we won't do any other semantic analysis now.
  if (CastTy->isDependentType() || CastExpr->isTypeDependent())
    return false;

  if (!CastTy->isLValueReferenceType() && !CastTy->isRecordType())
    DefaultFunctionArrayLvalueConversion(CastExpr);

  // C++ [expr.cast]p5: The conversions performed by
  //   - a const_cast,
  //   - a static_cast,
  //   - a static_cast followed by a const_cast,
  //   - a reinterpret_cast, or
  //   - a reinterpret_cast followed by a const_cast,
  //   can be performed using the cast notation of explicit type conversion.
  //   [...] If a conversion can be interpreted in more than one of the ways
  //   listed above, the interpretation that appears first in the list is used,
  //   even if a cast resulting from that interpretation is ill-formed.
  // In plain language, this means trying a const_cast ...
  unsigned msg = diag::err_bad_cxx_cast_generic;
  TryCastResult tcr = TryConstCast(*this, CastExpr, CastTy, /*CStyle*/true,
                                   msg);
  if (tcr == TC_Success)
    Kind = CK_NoOp;

  if (tcr == TC_NotApplicable) {
    // ... or if that is not possible, a static_cast, ignoring const, ...
    tcr = TryStaticCast(*this, CastExpr, CastTy, /*CStyle*/true, R, msg, Kind,
                        BasePath);
    if (tcr == TC_NotApplicable) {
      // ... and finally a reinterpret_cast, ignoring const.
      tcr = TryReinterpretCast(*this, CastExpr, CastTy, /*CStyle*/true, R, msg,
                               Kind);
    }
  }

  if (tcr != TC_Success && msg != 0)
    Diag(R.getBegin(), msg) << (FunctionalStyle ? CT_Functional : CT_CStyle)
      << CastExpr->getType() << CastTy << R;
  else if (Kind == CK_Unknown || Kind == CK_BitCast)
    CheckCastAlign(CastExpr, CastTy, R);

  return tcr != TC_Success;
}
