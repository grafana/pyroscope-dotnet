﻿// <copyright file="ProductDetails.cs" company="Datadog">
// Unless explicitly stated otherwise all files in this repository are licensed under the Apache 2 License.
// This product includes software developed at Datadog (https://www.datadoghq.com/). Copyright 2022 Datadog, Inc.
// </copyright>

namespace BuggyBits.Models
{
    public class ProductDetails
    {
        public string ProductName { get; set; }
        public ShippingInfo ShippingInfo { get; set; }
    }
}
