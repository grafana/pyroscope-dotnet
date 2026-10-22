#[derive(Clone, PartialEq, prost::Message)]
pub struct PushRequest {
    #[prost(message, repeated, tag = "1")]
    pub series: Vec<RawProfileSeries>,
}

#[derive(Clone, PartialEq, prost::Message)]
pub struct RawProfileSeries {
    #[prost(message, repeated, tag = "1")]
    pub labels: Vec<LabelPair>,
    #[prost(message, repeated, tag = "2")]
    pub samples: Vec<RawSample>,
}

#[derive(Clone, PartialEq, prost::Message)]
pub struct LabelPair {
    #[prost(string, tag = "1")]
    pub name: String,
    #[prost(string, tag = "2")]
    pub value: String,
}

#[derive(Clone, PartialEq, prost::Message)]
pub struct RawSample {
    #[prost(bytes = "vec", tag = "1")]
    pub raw_profile: Vec<u8>,
    #[prost(string, tag = "2")]
    pub id: String,
}
