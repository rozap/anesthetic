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
                if self.buf.len() > (self.size * 2) {
                    self.buf = self.buf[self.size..self.buf.len()].to_vec();
                }
                Ok(())
            }
            Err(e) => Err(format!("Failed to parse as uint: {}", e)),
        }
    }

    pub fn last_n(&self, n: usize, offset: usize) -> Vec<(usize, i16)> {
        let curs = n + offset;
        let start = if curs > self.buf.len() {
            0
        } else {
            (self.buf.len() - curs).max(0)
        };
        let end = if self.buf.len() > 0 {
            (start + n).min(self.buf.len() - 1)
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
