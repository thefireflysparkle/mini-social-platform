# DB Design for E-commerce

## 1. Entities

**users**: user_id (PK), email (UNIQUE), name, created_at.

**products**: product_id (PK), sku (UNIQUE), name, description, price_cents, created_at.

**inventory**: product_id (PK, FK to products), quantity, updated_at.

**carts**: cart_id (PK), user_id (FK, UNIQUE), updated_at.

**cart_items**: cart_id (FK), product_id (FK), quantity. Primary key is (cart_id, product_id).

**orders**: order_id (PK), user_id (FK), status, total_cents, created_at.

**order_items**: order_id (FK), product_id (FK), quantity, unit_price_cents. Primary key is (order_id, product_id).

All money is stored as integer cents. A float cannot represent 0.10 exactly, so adding up prices in floating point drifts by fractions of a paisa, and an order total that is off by one unit is a real bug.

## 2. Relationships

A user has one cart. The UNIQUE on carts.user_id enforces that.

A cart holds many products and a product can sit in many carts, so that is many-to-many through cart_items.

A user has many orders.

An order contains many products and a product appears in many orders, many-to-many through order_items.

A product has exactly one inventory row.

I kept inventory as its own table instead of a quantity column on products. Products are read constantly, every page view, and almost never written. Inventory is written on every single sale. Putting both in one row means every sale locks the row that every product page is trying to read. Splitting them keeps the hot write away from the hot read.

## 3. Placing an order

This is where SQL earns its place. Placing an order has to do four things together: insert the order, insert its items, decrease inventory, and empty the cart. If the server crashes halfway through, I either charge for stock I do not have or lose the order. So all four run inside one transaction.

To stop two people buying the last item at the same time, the inventory update checks stock in the same statement:

```sql
UPDATE inventory
SET quantity = quantity - ?
WHERE product_id = ? AND quantity >= ?;
```

If it changes zero rows, there was not enough stock, and the whole transaction rolls back. Reading the quantity first and then updating it separately would leave a gap between the two where another order can sneak in, which is how overselling happens.

## 4. Key indexes

**orders (user_id, created_at DESC)**: "my order history, newest first". The user is the filter, and putting created_at second means the rows come out already sorted.

**orders (status, created_at)**: the warehouse queue, "pending orders, oldest first". Without it, finding pending orders means scanning every order ever placed, and almost all of those are long delivered.

**order_items (product_id)**: "how many of this product sold". The primary key starts with order_id, so it cannot answer a question that starts from the product. Same leftmost prefix rule as follows in the social schema.

**products (sku)**: already indexed by its UNIQUE constraint, used by the admin side to look up a product by its code.

cart_items does not need an extra index. Loading a cart filters on cart_id, which is the leftmost column of its primary key.

## 5. Denormalization: copying the price into order_items

order_items.unit_price_cents is a copy of products.price_cents at the moment of purchase. In 3NF terms it is redundant, because the price already lives on the product.

I copy it anyway because prices change. If an order only stored product_id, then raising a price next month would silently change what every old order says the customer paid. Invoices, refunds and tax records would all be wrong. The order has to remember the price as it was, so the copy is not a performance shortcut, it is the only correct answer.

The cost is that the two values will disagree once the product price changes. That disagreement is exactly the point, so nothing needs to keep them in sync. It also means reading an order never joins to products for prices, which is a small speed win on top.

orders.total_cents is the same idea. It is the sum of the order's lines, stored so the order list page does not have to re-add every order's items. It is written once when the order is placed and never changes afterwards, so there is no risk of it going stale.
