//------------------------------------------------------------------------------
// <auto-generated />
// This file was automatically generated by the UpdateVendors tool.
//------------------------------------------------------------------------------
#pragma warning disable CS0618, CS0649, CS1574, CS1580, CS1581, CS1584, CS1591, CS1573, CS8018, SYSLIB0011, SYSLIB0032
#pragma warning disable CS8600, CS8601, CS8602, CS8603, CS8604, CS8618, CS8620, CS8714, CS8762, CS8765, CS8766, CS8767, CS8768, CS8769, CS8612, CS8629, CS8774
#nullable enable
// Decompiled with JetBrains decompiler
// Type: System.Reflection.Metadata.Ecma335.TypeRefTableReader
// Assembly: System.Reflection.Metadata, Version=7.0.0.2, Culture=neutral, PublicKeyToken=b03f5f7f11d50a3a
// MVID: 2EB35F4B-CF50-496F-AFB8-CC6F6F79CB72

using Datadog.Trace.VendoredMicrosoftCode.System.Reflection.Internal;

namespace Datadog.Trace.VendoredMicrosoftCode.System.Reflection.Metadata.Ecma335
{
  internal readonly struct TypeRefTableReader
  {
    internal readonly int NumberOfRows;
    private readonly bool _IsResolutionScopeRefSizeSmall;
    private readonly bool _IsStringHeapRefSizeSmall;
    private readonly int _ResolutionScopeOffset;
    private readonly int _NameOffset;
    private readonly int _NamespaceOffset;
    internal readonly int RowSize;
    internal readonly MemoryBlock Block;

    internal TypeRefTableReader(
      int numberOfRows,
      int resolutionScopeRefSize,
      int stringHeapRefSize,
      MemoryBlock containingBlock,
      int containingBlockOffset)
    {
      this.NumberOfRows = numberOfRows;
      this._IsResolutionScopeRefSizeSmall = resolutionScopeRefSize == 2;
      this._IsStringHeapRefSizeSmall = stringHeapRefSize == 2;
      this._ResolutionScopeOffset = 0;
      this._NameOffset = this._ResolutionScopeOffset + resolutionScopeRefSize;
      this._NamespaceOffset = this._NameOffset + stringHeapRefSize;
      this.RowSize = this._NamespaceOffset + stringHeapRefSize;
      this.Block = containingBlock.GetMemoryBlockAt(containingBlockOffset, this.RowSize * numberOfRows);
    }

    internal EntityHandle GetResolutionScope(TypeReferenceHandle handle) => ResolutionScopeTag.ConvertToHandle(this.Block.PeekTaggedReference((handle.RowId - 1) * this.RowSize + this._ResolutionScopeOffset, this._IsResolutionScopeRefSizeSmall));

    internal StringHandle GetName(TypeReferenceHandle handle) => StringHandle.FromOffset(this.Block.PeekHeapReference((handle.RowId - 1) * this.RowSize + this._NameOffset, this._IsStringHeapRefSizeSmall));

    internal StringHandle GetNamespace(TypeReferenceHandle handle) => StringHandle.FromOffset(this.Block.PeekHeapReference((handle.RowId - 1) * this.RowSize + this._NamespaceOffset, this._IsStringHeapRefSizeSmall));
  }
}