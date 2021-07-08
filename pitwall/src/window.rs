pub struct Window {
    buf: Vec<i16>,
    size: usize,
}

impl Window {
    pub fn new(size: usize) -> Window {
        Window {
            size: size,
            buf: vec![0; size],
        }
    }

    pub fn last(&self) -> Option<&i16> {
        self.buf.last()
    }

    pub fn put(&mut self, value: &str) -> Result<(), String> {
        match value.parse::<i16>() {
            Ok(v) => {
                self.buf.push(v);
                Ok(())
            }
            Err(e) => Err(format!("Failed to parse as uint: {}", e)),
        }
    }

    pub fn last_n(&self, n: usize) -> Vec<(usize, i16)> {
        let start = if n > self.buf.len() {
            0
        } else {
            (self.buf.len() - n).max(0)
        };
        let end = if self.buf.len() > 0 {
            self.buf.len() - 1
        } else {
            0
        };

        return self.buf[start..end]
            .iter()
            .enumerate()
            .map(|(i, v)| (i, v.to_owned()))
            .collect();
    }
}
