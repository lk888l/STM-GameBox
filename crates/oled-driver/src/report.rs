/// Statistics from one transport operation.
#[derive(Debug, Default, Clone, Copy, PartialEq, Eq)]
pub struct TransferReport {
    /// Payload bytes, excluding protocol control bytes.
    pub payload_bytes: u32,
    /// Physical bus transactions used for the payload.
    pub transactions: u16,
}

impl TransferReport {
    pub(crate) const fn new(payload_bytes: usize, transactions: usize) -> Self {
        Self {
            payload_bytes: payload_bytes as u32,
            transactions: transactions as u16,
        }
    }
}

/// Statistics from a display initialization or flush.
#[derive(Debug, Default, Clone, Copy, PartialEq, Eq)]
pub struct FlushReport {
    /// Controller command bytes sent.
    pub command_bytes: u32,
    /// Display data bytes sent.
    pub data_bytes: u32,
    /// Physical command transactions.
    pub command_transactions: u16,
    /// Physical data transactions.
    pub data_transactions: u16,
    /// Independently addressed framebuffer regions.
    pub regions: u16,
    /// Physical display pages touched.
    pub pages: u8,
}

impl FlushReport {
    pub(crate) fn add_commands(&mut self, report: TransferReport) {
        self.command_bytes = self.command_bytes.saturating_add(report.payload_bytes);
        self.command_transactions = self
            .command_transactions
            .saturating_add(report.transactions);
    }

    pub(crate) fn add_data(&mut self, report: TransferReport) {
        self.data_bytes = self.data_bytes.saturating_add(report.payload_bytes);
        self.data_transactions = self.data_transactions.saturating_add(report.transactions);
    }

    pub(crate) fn merge(&mut self, other: Self) {
        self.command_bytes = self.command_bytes.saturating_add(other.command_bytes);
        self.data_bytes = self.data_bytes.saturating_add(other.data_bytes);
        self.command_transactions = self
            .command_transactions
            .saturating_add(other.command_transactions);
        self.data_transactions = self
            .data_transactions
            .saturating_add(other.data_transactions);
        self.regions = self.regions.saturating_add(other.regions);
        self.pages = self.pages.saturating_add(other.pages);
    }
}
