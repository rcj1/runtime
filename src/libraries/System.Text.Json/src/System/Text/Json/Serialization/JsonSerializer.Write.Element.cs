// Licensed to the .NET Foundation under one or more agreements.
// The .NET Foundation licenses this file to you under the MIT license.

using System.Diagnostics;
using System.Diagnostics.CodeAnalysis;
using System.Text.Json.Serialization;
using System.Text.Json.Serialization.Metadata;

namespace System.Text.Json
{
    public static partial class JsonSerializer
    {
        /// <summary>
        /// Converts the provided value into a <see cref="JsonElement"/>.
        /// </summary>
        /// <typeparam name="TValue">The type of the value to serialize.</typeparam>
        /// <returns>A <see cref="JsonElement"/> representation of the JSON value.</returns>
        /// <param name="value">The value to convert.</param>
        /// <param name="options">Options to control the conversion behavior.</param>
        /// <exception cref="NotSupportedException">
        /// There is no compatible <see cref="System.Text.Json.Serialization.JsonConverter"/>
        /// for <typeparamref name="TValue"/> or its serializable members.
        /// </exception>
        [RequiresUnreferencedCode(SerializationUnreferencedCodeMessage)]
        [RequiresDynamicCode(SerializationRequiresDynamicCodeMessage)]
        public static JsonElement SerializeToElement<TValue>(TValue value, JsonSerializerOptions? options = null)
        {
            JsonTypeInfo<TValue> jsonTypeInfo = GetTypeInfo<TValue>(options);
            return WriteElement(value, jsonTypeInfo);
        }

        /// <summary>
        /// Converts the provided value into a <see cref="JsonElement"/>.
        /// </summary>
        /// <returns>A <see cref="JsonElement"/> representation of the value.</returns>
        /// <param name="value">The value to convert.</param>
        /// <param name="inputType">The type of the <paramref name="value"/> to convert.</param>
        /// <param name="options">Options to control the conversion behavior.</param>
        /// <exception cref="ArgumentException">
        /// <paramref name="inputType"/> is not compatible with <paramref name="value"/>.
        /// </exception>
        /// <exception cref="NotSupportedException">
        /// <exception cref="ArgumentNullException">
        /// <paramref name="inputType"/> is <see langword="null"/>.
        /// </exception>
        /// There is no compatible <see cref="System.Text.Json.Serialization.JsonConverter"/>
        /// for <paramref name="inputType"/>  or its serializable members.
        /// </exception>
        [RequiresUnreferencedCode(SerializationUnreferencedCodeMessage)]
        [RequiresDynamicCode(SerializationRequiresDynamicCodeMessage)]
        public static JsonElement SerializeToElement(object? value, Type inputType, JsonSerializerOptions? options = null)
        {
            ValidateInputType(value, inputType);
            JsonTypeInfo jsonTypeInfo = GetTypeInfo(options, inputType);
            return WriteElementAsObject(value, jsonTypeInfo);
        }

        /// <summary>
        /// Converts the provided value into a <see cref="JsonElement"/>.
        /// </summary>
        /// <typeparam name="TValue">The type of the value to serialize.</typeparam>
        /// <returns>A <see cref="JsonElement"/> representation of the value.</returns>
        /// <param name="value">The value to convert.</param>
        /// <param name="jsonTypeInfo">Metadata about the type to convert.</param>
        /// <exception cref="ArgumentNullException">
        /// <paramref name="jsonTypeInfo"/> is <see langword="null"/>.
        /// </exception>
        public static JsonElement SerializeToElement<TValue>(TValue value, JsonTypeInfo<TValue> jsonTypeInfo)
        {
            ArgumentNullException.ThrowIfNull(jsonTypeInfo);

            jsonTypeInfo.EnsureConfigured();
            return WriteElement(value, jsonTypeInfo);
        }

        /// <summary>
        /// Converts the provided value into a <see cref="JsonElement"/>.
        /// </summary>
        /// <returns>A <see cref="JsonElement"/> representation of the value.</returns>
        /// <param name="value">The value to convert.</param>
        /// <param name="jsonTypeInfo">Metadata about the type to convert.</param>
        /// <exception cref="ArgumentNullException">
        /// <paramref name="jsonTypeInfo"/> is <see langword="null"/>.
        /// </exception>
        /// <exception cref="InvalidCastException">
        /// <paramref name="value"/> does not match the type of <paramref name="jsonTypeInfo"/>.
        /// </exception>
        public static JsonElement SerializeToElement(object? value, JsonTypeInfo jsonTypeInfo)
        {
            ArgumentNullException.ThrowIfNull(jsonTypeInfo);

            jsonTypeInfo.EnsureConfigured();
            return WriteElementAsObject(value, jsonTypeInfo);
        }

        /// <summary>
        /// Converts the provided value into a <see cref="JsonElement"/>.
        /// </summary>
        /// <returns>A <see cref="JsonElement"/> representation of the value.</returns>
        /// <param name="value">The value to convert.</param>
        /// <param name="inputType">The type of the <paramref name="value"/> to convert.</param>
        /// <param name="context">A metadata provider for serializable types.</param>
        /// <exception cref="NotSupportedException">
        /// There is no compatible <see cref="System.Text.Json.Serialization.JsonConverter"/>
        /// for <paramref name="inputType"/> or its serializable members.
        /// </exception>
        /// <exception cref="InvalidOperationException">
        /// The <see cref="JsonSerializerContext.GetTypeInfo(Type)"/> method of the provided
        /// <paramref name="context"/> returns <see langword="null"/> for the type to convert.
        /// </exception>
        /// <exception cref="ArgumentNullException">
        /// <paramref name="inputType"/> or <paramref name="context"/> is <see langword="null"/>.
        /// </exception>
        public static JsonElement SerializeToElement(object? value, Type inputType, JsonSerializerContext context)
        {
            ArgumentNullException.ThrowIfNull(context);

            ValidateInputType(value, inputType);
            JsonTypeInfo typeInfo = GetTypeInfo(context, inputType);
            return WriteElementAsObject(value, typeInfo);
        }

        private static JsonElement WriteElement<TValue>(in TValue value, JsonTypeInfo<TValue> jsonTypeInfo)
        {
            Debug.Assert(jsonTypeInfo.IsConfigured);
            JsonSerializerOptions options = jsonTypeInfo.Options;

            Utf8JsonWriter writer = Utf8JsonWriterCache.RentWriterAndBuffer(jsonTypeInfo.Options, out PooledByteBufferWriter output);

            try
            {
                jsonTypeInfo.Serialize(writer, value);
                return JsonElement.Parse(output.WrittenSpan, options.GetDocumentOptions());
            }
            finally
            {
                Utf8JsonWriterCache.ReturnWriterAndBuffer(writer, output);
            }
        }

        private static JsonElement WriteElementAsObject(object? value, JsonTypeInfo jsonTypeInfo)
        {
            JsonSerializerOptions options = jsonTypeInfo.Options;
            Debug.Assert(options != null);

            Utf8JsonWriter writer = Utf8JsonWriterCache.RentWriterAndBuffer(jsonTypeInfo.Options, out PooledByteBufferWriter output);

            try
            {
                jsonTypeInfo.SerializeAsObject(writer, value);
                return JsonElement.Parse(output.WrittenSpan, options.GetDocumentOptions());
            }
            finally
            {
                Utf8JsonWriterCache.ReturnWriterAndBuffer(writer, output);
            }
        }
    }
}
